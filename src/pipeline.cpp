#include "pipeline.hpp"

#include "adaptive.hpp"
#include "dependencies.hpp"
#include "hls_server.hpp"
#include "mailbox.hpp"
#include "metrics.hpp"
#include "processor.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

namespace rtx {
namespace {

std::atomic<bool>* g_running = nullptr;

void on_signal(int) {
  if (g_running != nullptr) {
    g_running->store(false);
  }
}

std::string ffmpeg_error(int status) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(status, buffer, sizeof(buffer));
  return buffer;
}

void throw_if(int status, const std::string& action) {
  if (status < 0) {
    throw std::runtime_error(action + ": " + ffmpeg_error(status));
  }
}

std::string redact(std::string url) {
  const std::string key = "passphrase=";
  const auto pos = url.find(key);
  if (pos == std::string::npos) {
    return url;
  }
  const auto end = url.find('&', pos);
  url.replace(pos + key.size(), end == std::string::npos ? std::string::npos : end - (pos + key.size()), "***");
  return url;
}

AVDictionary* to_dict(const std::map<std::string, std::string>& options) {
  AVDictionary* dict = nullptr;
  for (const auto& option : options) {
    av_dict_set(&dict, option.first.c_str(), option.second.c_str(), 0);
  }
  return dict;
}

void ensure_consumed(AVDictionary*& dict, const std::string& where) {
  if (dict == nullptr) {
    return;
  }
  AVDictionaryEntry* entry = av_dict_get(dict, "", nullptr, AV_DICT_IGNORE_SUFFIX);
  if (entry != nullptr) {
    const std::string key = entry->key;
    av_dict_free(&dict);
    throw std::runtime_error(where + ": FFmpeg не прийняв параметр " + key);
  }
  av_dict_free(&dict);
}

AVPixelFormat cuda_only(AVCodecContext*, const AVPixelFormat* formats) {
  for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
    if (*format == AV_PIX_FMT_CUDA) {
      return *format;
    }
  }
  return AV_PIX_FMT_NONE;
}

int interrupt_cb(void* opaque) {
  auto* running = static_cast<std::atomic<bool>*>(opaque);
  return running->load() ? 0 : 1;
}

void free_frame(AVFrame* frame) {
  AVFrame* local = frame;
  av_frame_free(&local);
}

bool try_set(void* object, const char* key, const char* value) {
  return object != nullptr && av_opt_set(object, key, value, 0) == 0;
}

void configure_nvenc(AVCodecContext* encoder) {
  encoder->max_b_frames = 0;
  encoder->gop_size = 15;
  encoder->keyint_min = 15;
  encoder->flags |= AV_CODEC_FLAG_LOW_DELAY;
  if (!try_set(encoder->priv_data, "preset", "llhp") && !try_set(encoder->priv_data, "preset", "p1") &&
      !try_set(encoder->priv_data, "preset", "fast")) {
    throw std::runtime_error("NVENC не прийняв жоден низьколатентний preset");
  }
  try_set(encoder->priv_data, "tune", "ull");
  try_set(encoder->priv_data, "zerolatency", "1");
  try_set(encoder->priv_data, "delay", "0");
  try_set(encoder->priv_data, "rc-lookahead", "0");
  try_set(encoder->priv_data, "bf", "0");
  try_set(encoder->priv_data, "b_adapt", "0");
  if (!try_set(encoder->priv_data, "rc", "cbr_ld_hq")) {
    try_set(encoder->priv_data, "rc", "cbr");
  }
}

std::int64_t choose_bitrate(const Settings& settings, int width, int height) {
  if (settings.bitrate > 0) {
    return settings.bitrate;
  }
  const std::int64_t pixels = static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
  return std::max<std::int64_t>(2'000'000, pixels * 3);
}

std::string same_file_error(const BuiltEndpoint& input, const BuiltEndpoint& output) {
  namespace fs = std::filesystem;
  std::error_code error;
  if (input.ffmpeg_url.empty() || output.ffmpeg_url.empty()) {
    return {};
  }
  const fs::path in_path(input.ffmpeg_url);
  const fs::path out_path(output.ffmpeg_url);
  if (!fs::is_regular_file(in_path, error)) {
    return {};
  }
  const auto in_canon = fs::weakly_canonical(in_path, error);
  const auto out_canon = fs::weakly_canonical(out_path, error);
  if (!error && in_canon == out_canon) {
    return "вхідний і вихідний файли збігаються";
  }
  return {};
}

void prepare_hls(BuiltEndpoint& output, HlsServer& server) {
  namespace fs = std::filesystem;
  if (output.serve_root.empty()) {
    output.serve_root = (fs::temp_directory_path() / ("rtx-vsr-hls-" + std::to_string(::getpid()))).string();
  }
  fs::create_directories(output.serve_root);
  output.ffmpeg_url = (fs::path(output.serve_root) / "index.m3u8").string();
  output.format_options.emplace("hls_time", "1");
  output.format_options.emplace("hls_list_size", "3");
  output.format_options.emplace("hls_flags", "delete_segments+append_list+omit_endlist+independent_segments");
  output.format_options.emplace("hls_segment_filename", (fs::path(output.serve_root) / "seg_%05d.ts").string());
  std::string error;
  if (!server.start(output.serve_host, output.serve_port, output.serve_root, error)) {
    throw std::runtime_error(error);
  }
}

struct Session {
  std::atomic<bool> running{true};
  AVFormatContext* input = nullptr;
  AVFormatContext* output = nullptr;
  AVCodecContext* decoder = nullptr;
  AVCodecContext* encoder = nullptr;
  AVBufferRef* hw_device = nullptr;
  AVBufferRef* hw_frames = nullptr;
  AVFrame* decode_frame = nullptr;
  AVPacket* packet = nullptr;
  std::unique_ptr<FrameProcessor> processor;
  LatestSlot<AVFrame> mailbox{free_frame};
  std::mutex metrics_mu;
  Metrics metrics;
  std::mutex error_mu;
  std::string error;
  bool header_written = false;
  int video_index = -1;
  int dst_w = 0;
  int dst_h = 0;
  double budget_ms = 33.3;
  AdaptiveQuality quality{2, 1, 4, true};

  explicit Session(const Settings& settings)
      : quality(settings.quality, settings.quality_min, settings.quality_max, settings.quality_auto) {}

  ~Session() {
    if (packet != nullptr) {
      av_packet_free(&packet);
    }
    if (decode_frame != nullptr) {
      av_frame_free(&decode_frame);
    }
    if (decoder != nullptr) {
      avcodec_free_context(&decoder);
    }
    if (encoder != nullptr) {
      avcodec_free_context(&encoder);
    }
    if (input != nullptr) {
      avformat_close_input(&input);
    }
    if (output != nullptr) {
      if (header_written) {
        av_write_trailer(output);
      }
      if (output->pb != nullptr && !(output->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output->pb);
      }
      avformat_free_context(output);
    }
    if (hw_frames != nullptr) {
      av_buffer_unref(&hw_frames);
    }
    if (hw_device != nullptr) {
      av_buffer_unref(&hw_device);
    }
  }

  void fail(const std::string& text) {
    std::lock_guard<std::mutex> lock(error_mu);
    if (error.empty()) {
      error = text;
    }
    running.store(false);
    mailbox.close();
  }

  void open_input(const BuiltEndpoint& built, const Settings& settings) {
    AVFormatContext* context = avformat_alloc_context();
    if (context == nullptr) {
      throw std::runtime_error("немає контексту входу");
    }
    context->interrupt_callback.callback = interrupt_cb;
    context->interrupt_callback.opaque = &running;
    const bool live = settings.input.protocol != "file";
    if (live) {
      context->flags |= AVFMT_FLAG_NOBUFFER;
      context->probesize = 256 * 1024;
      context->max_analyze_duration = 300000;
      context->max_delay = 0;
    }
    const std::string format_name = settings.input_format.empty() ? built.format : settings.input_format;
    const AVInputFormat* format = nullptr;
    if (!format_name.empty()) {
      format = av_find_input_format(format_name.c_str());
      if (format == nullptr) {
        avformat_free_context(context);
        throw std::runtime_error("демуксер " + format_name + " недоступний");
      }
    }
    AVDictionary* options = to_dict(built.format_options);
    const int status = avformat_open_input(&context, built.ffmpeg_url.c_str(), const_cast<AVInputFormat*>(format), &options);
    if (status < 0) {
      av_dict_free(&options);
      throw std::runtime_error("відкриття входу: " + ffmpeg_error(status));
    }
    input = context;
    ensure_consumed(options, "вхід");
    throw_if(avformat_find_stream_info(input, nullptr), "параметри потоку");
    video_index = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_index < 0) {
      throw std::runtime_error("у вході немає відеопотоку");
    }
  }

  void open_decoder(const Settings& settings) {
    AVStream* stream = input->streams[video_index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
      throw std::runtime_error("немає декодера для вхідного відео");
    }
    throw_if(av_hwdevice_ctx_create(&hw_device, AV_HWDEVICE_TYPE_CUDA, std::to_string(settings.gpu).c_str(), nullptr, 0),
             "NVDEC/CUDA");
    decoder = avcodec_alloc_context3(codec);
    if (decoder == nullptr) {
      throw std::runtime_error("немає контексту декодера");
    }
    throw_if(avcodec_parameters_to_context(decoder, stream->codecpar), "параметри декодера");
    decoder->pkt_timebase = stream->time_base;
    decoder->thread_count = 1;
    decoder->flags |= AV_CODEC_FLAG_LOW_DELAY;
    decoder->extra_hw_frames = 2;
    decoder->get_format = cuda_only;
    decoder->hw_device_ctx = av_buffer_ref(hw_device);
    AVDictionary* options = nullptr;
    av_dict_set(&options, "flags", "low_delay", 0);
    const int status = avcodec_open2(decoder, codec, &options);
    av_dict_free(&options);
    throw_if(status, "запуск NVDEC");
    if (decoder->width < 2 || decoder->height < 2 || decoder->width % 2 != 0 || decoder->height % 2 != 0) {
      throw std::runtime_error("NVDEC і NVENC потребують парних ширини й висоти");
    }
  }

  void open_processor_and_encoder(const Settings& settings, BuiltEndpoint& built) {
    processor = create_frame_processor();
    if (!processor) {
      throw std::runtime_error("обробник RTX Video SDK не зібрано");
    }
    const int initial = quality.quality();
    if (!processor->open(hw_device, decoder->width, decoder->height, settings.scale, initial)) {
      throw std::runtime_error(processor->last_error());
    }
    dst_w = processor->output_width();
    dst_h = processor->output_height();
    const char* encoder_name = settings.codec == "hevc" ? "hevc_nvenc" : "h264_nvenc";
    const AVCodec* codec = avcodec_find_encoder_by_name(encoder_name);
    if (codec == nullptr) {
      throw std::runtime_error(std::string("немає кодера ") + encoder_name);
    }
    const std::string format_name = settings.output_format.empty() ? built.format : settings.output_format;
    const int status = avformat_alloc_output_context2(&output, nullptr, format_name.empty() ? nullptr : format_name.c_str(),
                                                      built.ffmpeg_url.c_str());
    throw_if(status, "контекст виходу");
    if (output == nullptr) {
      throw std::runtime_error("немає контексту виходу");
    }
    output->interrupt_callback.callback = interrupt_cb;
    output->interrupt_callback.opaque = &running;
    output->max_delay = 0;
    output->flags |= AVFMT_FLAG_FLUSH_PACKETS | AVFMT_FLAG_NOBUFFER;

    encoder = avcodec_alloc_context3(codec);
    if (encoder == nullptr) {
      throw std::runtime_error("немає контексту NVENC");
    }
    AVStream* in_stream = input->streams[video_index];
    AVRational rate = in_stream->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) {
      rate = in_stream->r_frame_rate;
    }
    if (rate.num <= 0 || rate.den <= 0) {
      rate = AVRational{30, 1};
    }
    budget_ms = 1000.0 * static_cast<double>(rate.den) / static_cast<double>(rate.num);
    encoder->width = dst_w;
    encoder->height = dst_h;
    encoder->framerate = rate;
    encoder->time_base = av_inv_q(rate);
    encoder->pix_fmt = AV_PIX_FMT_CUDA;
    encoder->color_range = AVCOL_RANGE_MPEG;
    encoder->colorspace = AVCOL_SPC_BT709;
    encoder->color_primaries = AVCOL_PRI_BT709;
    encoder->color_trc = AVCOL_TRC_BT709;
    const std::int64_t bitrate = choose_bitrate(settings, dst_w, dst_h);
    encoder->bit_rate = bitrate;
    encoder->rc_max_rate = bitrate;
    encoder->rc_buffer_size = static_cast<int>(bitrate / 8);
    hw_frames = av_hwframe_ctx_alloc(hw_device);
    if (hw_frames == nullptr) {
      throw std::runtime_error("немає пулу кадрів CUDA");
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(hw_frames->data);
    frames->format = AV_PIX_FMT_CUDA;
    frames->sw_format = AV_PIX_FMT_NV12;
    frames->width = dst_w;
    frames->height = dst_h;
    frames->initial_pool_size = 4;
    throw_if(av_hwframe_ctx_init(hw_frames), "ініціалізація кадрів CUDA");
    encoder->hw_frames_ctx = av_buffer_ref(hw_frames);
    encoder->hw_device_ctx = av_buffer_ref(hw_device);
    if (output->oformat->flags & AVFMT_GLOBALHEADER) {
      encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    configure_nvenc(encoder);
    throw_if(avcodec_open2(encoder, codec, nullptr), "запуск NVENC");

    AVStream* out_stream = avformat_new_stream(output, nullptr);
    if (out_stream == nullptr) {
      throw std::runtime_error("немає вихідного потоку");
    }
    throw_if(avcodec_parameters_from_context(out_stream->codecpar, encoder), "параметри NVENC");
    out_stream->time_base = encoder->time_base;
    for (const auto& option : built.format_options) {
      const int option_status = av_opt_set(output->priv_data, option.first.c_str(), option.second.c_str(), 0);
      if (option_status < 0) {
        throw std::runtime_error("параметр виходу " + option.first + ": " + ffmpeg_error(option_status));
      }
    }
    if (!(output->oformat->flags & AVFMT_NOFILE)) {
      AVDictionary* io_options = nullptr;
      throw_if(avio_open2(&output->pb, built.ffmpeg_url.c_str(), AVIO_FLAG_WRITE, &output->interrupt_callback, &io_options),
               "відкриття виходу");
      av_dict_free(&io_options);
    }
    throw_if(avformat_write_header(output, nullptr), "заголовок виходу");
    header_written = true;
    metrics.vsr_quality = processor->quality();
    metrics.vsr_scale = settings.scale;
    metrics.vsr_state = "running";
  }

  void write_packet(AVPacket* encoded) {
    AVStream* stream = output->streams[0];
    av_packet_rescale_ts(encoded, encoder->time_base, stream->time_base);
    encoded->stream_index = stream->index;
    throw_if(av_interleaved_write_frame(output, encoded), "запис виходу");
  }

  void drain_encoder() {
    while (running.load()) {
      const int status = avcodec_receive_packet(encoder, packet);
      if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
        return;
      }
      throw_if(status, "пакет NVENC");
      write_packet(packet);
      av_packet_unref(packet);
    }
  }

  void decode_loop() {
    AVPacket* incoming = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (incoming == nullptr || frame == nullptr) {
      fail("немає буфера декодера");
      av_packet_free(&incoming);
      av_frame_free(&frame);
      return;
    }
    while (running.load()) {
      const int read = av_read_frame(input, incoming);
      if (read == AVERROR_EOF || read == AVERROR_EXIT) {
        break;
      }
      if (read < 0) {
        fail("читання входу: " + ffmpeg_error(read));
        break;
      }
      if (incoming->stream_index != video_index) {
        av_packet_unref(incoming);
        continue;
      }
      int sent = avcodec_send_packet(decoder, incoming);
      av_packet_unref(incoming);
      if (sent < 0 && sent != AVERROR(EAGAIN)) {
        fail("NVDEC: " + ffmpeg_error(sent));
        break;
      }
      while (running.load()) {
        const int received = avcodec_receive_frame(decoder, frame);
        if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
          break;
        }
        if (received < 0) {
          fail("кадр NVDEC: " + ffmpeg_error(received));
          break;
        }
        if (frame->format != AV_PIX_FMT_CUDA) {
          fail("декодер віддав кадр не в пам'яті GPU");
          break;
        }
        {
          std::lock_guard<std::mutex> lock(metrics_mu);
          ++metrics.frames_in;
        }
        AVFrame* owned = av_frame_alloc();
        av_frame_move_ref(owned, frame);
        mailbox.publish(owned);
      }
    }
    av_packet_free(&incoming);
    av_frame_free(&frame);
    running.store(false);
    mailbox.close();
  }

  void process_loop(const Settings& settings) {
    packet = av_packet_alloc();
    auto last_report = std::chrono::steady_clock::now();
    std::uint64_t window_out = 0;
    while (true) {
      AVFrame* input_frame = mailbox.take(running, std::chrono::milliseconds(50));
      if (input_frame == nullptr) {
        if (!running.load()) {
          break;
        }
        continue;
      }
      AVFrame* output_frame = av_frame_alloc();
      if (output_frame == nullptr || av_hwframe_get_buffer(encoder->hw_frames_ctx, output_frame, 0) < 0) {
        free_frame(input_frame);
        av_frame_free(&output_frame);
        fail("немає вихідного кадру CUDA");
        break;
      }
      output_frame->pts = input_frame->pts;
      double process_ms = 0;
      const bool ok = processor->process(input_frame, output_frame, process_ms);
      free_frame(input_frame);
      if (!ok) {
        av_frame_free(&output_frame);
        fail(processor->last_error());
        break;
      }
      const bool changed = quality.observe(process_ms, budget_ms);
      processor->set_quality(quality.quality());
      if (changed) {
        std::cerr << "VSR quality -> " << quality.quality() << '\n';
      }
      int sent = AVERROR(EAGAIN);
      for (int attempt = 0; attempt < 3 && sent == AVERROR(EAGAIN); ++attempt) {
        if (attempt > 0) {
          try {
            drain_encoder();
          } catch (const std::exception& ex) {
            av_frame_free(&output_frame);
            fail(ex.what());
            return;
          }
        }
        sent = avcodec_send_frame(encoder, output_frame);
      }
      av_frame_free(&output_frame);
      if (sent < 0) {
        fail("NVENC: " + ffmpeg_error(sent));
        break;
      }
      try {
        drain_encoder();
      } catch (const std::exception& ex) {
        fail(ex.what());
        break;
      }
      {
        std::lock_guard<std::mutex> lock(metrics_mu);
        ++metrics.frames_out;
        ++window_out;
        metrics.frames_dropped = mailbox.dropped();
        metrics.last_process_ms = process_ms;
        metrics.avg_process_ms = metrics.avg_process_ms == 0 ? process_ms : metrics.avg_process_ms * 0.9 + process_ms * 0.1;
        metrics.vsr_quality = processor->quality();
        metrics.vsr_state = processor->quality() < settings.quality_max ? "degraded" : "running";
      }
      const auto now = std::chrono::steady_clock::now();
      if (settings.metrics_interval > 0 && now - last_report >= std::chrono::duration<double>(settings.metrics_interval)) {
        const double seconds = std::chrono::duration<double>(now - last_report).count();
        Metrics snapshot;
        {
          std::lock_guard<std::mutex> lock(metrics_mu);
          metrics.fps = seconds > 0 ? static_cast<double>(window_out) / seconds : 0;
          snapshot = metrics;
        }
        std::cerr << metrics_json(snapshot) << '\n';
        if (!settings.metrics_file.empty() && !write_metrics_file(settings.metrics_file, snapshot)) {
          std::cerr << "не вдалося записати файл метрик\n";
        }
        window_out = 0;
        last_report = now;
      }
    }
    if (error.empty()) {
      avcodec_send_frame(encoder, nullptr);
      try {
        running.store(true);
        drain_encoder();
        running.store(false);
      } catch (const std::exception& ex) {
        fail(ex.what());
      }
    }
  }
};

}  // namespace

int run_proxy(const Settings& settings, const std::vector<Mode>& modes, const OptionProbe& probe) {
  const Mode* input_mode = find_mode(modes, Direction::Input, settings.input.protocol, settings.input.role);
  const Mode* output_mode = find_mode(modes, Direction::Output, settings.output.protocol, settings.output.role);
  if (input_mode == nullptr) {
    std::cerr << "вхідний режим недоступний у цій збірці: " << settings.input.protocol << " " << settings.input.role
              << '\n';
    return 2;
  }
  if (output_mode == nullptr) {
    std::cerr << "вихідний режим недоступний у цій збірці: " << settings.output.protocol << " "
              << settings.output.role << '\n';
    return 2;
  }
  BuiltEndpoint input_built;
  BuiltEndpoint output_built;
  const std::string input_error = build_endpoint(settings.input, *input_mode, probe, input_built);
  if (!input_error.empty()) {
    std::cerr << input_error << '\n';
    return 2;
  }
  const std::string output_error = build_endpoint(settings.output, *output_mode, probe, output_built);
  if (!output_error.empty()) {
    std::cerr << output_error << '\n';
    return 2;
  }
  const std::string same = same_file_error(input_built, output_built);
  if (!same.empty()) {
    std::cerr << same << '\n';
    return 2;
  }
  const DependencyReport report = inspect_dependencies();
  const std::string blockers = dependency_blockers(settings, report);
  if (!blockers.empty()) {
    std::cerr << blockers;
    return 3;
  }
  if (settings.scale != 2 && settings.scale != 3) {
    std::cerr << "RTX Video SDK у цьому проксі використовує масштаб 2 або 3\n";
    return 2;
  }

  HlsServer hls;
  try {
    if (output_built.application_hls) {
      prepare_hls(output_built, hls);
    } else if (settings.output.protocol == "file") {
      const std::filesystem::path parent = std::filesystem::path(output_built.ffmpeg_url).parent_path();
      if (!parent.empty()) {
        std::filesystem::create_directories(parent);
      }
    }
    std::cerr << "вхід: " << redact(input_built.public_url) << '\n';
    std::cerr << "вихід: " << redact(output_built.public_url) << '\n';
    Session session(settings);
    g_running = &session.running;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);
    session.open_input(input_built, settings);
    session.open_decoder(settings);
    session.open_processor_and_encoder(settings, output_built);
    std::thread decoder([&session] { session.decode_loop(); });
    std::thread processor([&session, &settings] { session.process_loop(settings); });
    decoder.join();
    processor.join();
    g_running = nullptr;
    std::lock_guard<std::mutex> lock(session.error_mu);
    if (!session.error.empty()) {
      std::cerr << session.error << '\n';
      return 1;
    }
    return 0;
  } catch (const std::exception& ex) {
    g_running = nullptr;
    std::cerr << ex.what() << '\n';
    return 1;
  }
}

}  // namespace rtx
