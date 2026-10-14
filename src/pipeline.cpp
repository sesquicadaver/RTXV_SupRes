#include "pipeline.hpp"

#include "profile.hpp"

#include "adaptive.hpp"
#include "color.hpp"
#include "dependencies.hpp"
#include "hls_server.hpp"
#include "latency.hpp"
#include "mailbox.hpp"
#include "metrics.hpp"
#include "platform.hpp"
#include "json_mini.hpp"
#include "report.hpp"
#include "processor.hpp"
#include "timestamps.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/version.h>
}

namespace rtx {

static bool g_emit_status = true;

namespace {

std::atomic<bool>* g_running = nullptr;

void on_signal(int) {
  if (g_running != nullptr) {
    g_running->store(false);
  }
}

struct FrameTiming {
  std::chrono::steady_clock::time_point decoded_at{};
  std::chrono::steady_clock::time_point ready_at{};
  double decode_ms = 0;
};

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

std::mutex g_emit_mu;

void emit_line(const std::string& line) {
  if (!g_emit_status) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_emit_mu);
  std::cout << line << '\n';
  std::cout.flush();
}

std::string redact(std::string url) {
  const auto scheme = url.find("://");
  if (scheme != std::string::npos) {
    const auto authority = scheme + 3;
    const auto slash = url.find('/', authority);
    const auto at = url.find('@', authority);
    if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
      url.replace(authority, at - authority, "***");
    }
  }
  const std::string key = "passphrase=";
  const auto pos = url.find(key);
  if (pos == std::string::npos) {
    return url;
  }
  const auto end = url.find('&', pos);
  url.replace(pos + key.size(), end == std::string::npos ? std::string::npos : end - (pos + key.size()), "***");
  return url;
}

double elapsed_ms(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
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
  if (frame != nullptr && frame->opaque != nullptr) {
    delete static_cast<FrameTiming*>(frame->opaque);
    frame->opaque = nullptr;
  }
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
    output.serve_root = (fs::temp_directory_path() / ("rtx-vsr-hls-" + std::to_string(process_id()))).string();
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

void apply_color(AVCodecContext* encoder, const ColorChoice& color) {
  encoder->colorspace = static_cast<AVColorSpace>(av_space(color));
  encoder->color_range = static_cast<AVColorRange>(av_range(color));
  encoder->color_primaries = static_cast<AVColorPrimaries>(av_primaries(color));
  encoder->color_trc = static_cast<AVColorTransferCharacteristic>(av_trc(color));
}

void set_frame_duration(AVFrame* frame) {
  // AVFrame::duration з'явилося у FFmpeg 6 (libavutil 58). У 5.1 (libavutil 57) є лише pkt_duration.
#if LIBAVUTIL_VERSION_MAJOR >= 58
  frame->duration = 1;
#else
  frame->pkt_duration = 1;
#endif
}

struct Session {
  std::atomic<bool> running{true};
  AVFormatContext* input = nullptr;
  AVFormatContext* output = nullptr;
  AVCodecContext* decoder = nullptr;
  AVCodecContext* encoder = nullptr;
  AVBufferRef* hw_device = nullptr;
  AVBufferRef* hw_frames = nullptr;
  AVPacket* packet = nullptr;
  std::unique_ptr<FrameProcessor> processor;
  LatestSlot<AVFrame> mailbox{free_frame};
  std::mutex metrics_mu;
  Metrics metrics;
  std::mutex error_mu;
  std::string error;
  bool header_written = false;
  bool bypass_logged = false;
  int video_index = -1;
  int active_scale = 2;
  double budget_ms = 33.3;
  double frame_write_ms = 0;
  AVRational input_tb{1, 90000};
  PtsMap pts;
  AdaptiveQuality quality{2, 1, 4, true};
  std::string sdp_path;
  bool pace_input = false;
  double warmup_s = 0;
  ProxyLatency latency;
  bool warmup_done = false;
  bool saw_bypass = false;
  std::string public_state;
  std::chrono::steady_clock::time_point measure_from{};
  std::uint64_t measured_frames = 0;
  std::chrono::steady_clock::time_point fps_origin{};
  std::chrono::steady_clock::time_point fps_last{};
  bool fps_origin_set = false;
  bool pace_origin_set = false;
  std::chrono::steady_clock::time_point pace_origin{};
  std::int64_t pace_origin_pts = AV_NOPTS_VALUE;

  explicit Session(const Settings& settings)
      : active_scale(settings.scale),
        quality(settings.quality, settings.quality_min, settings.quality_max, settings.quality_auto),
        pace_input(settings.pace_input),
        warmup_s(settings.warmup_s) {}

  ~Session() {
    // Деструктор unique_ptr виконується після тіла. CUDA-контекст процесора належить hw_device,
    // тому ефект треба закрити, доки контекст ще живий.
    if (processor) {
      processor->close();
      processor.reset();
    }
    if (packet != nullptr) {
      av_packet_free(&packet);
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
      emit_line(status_event("error", text));
    }
    running.store(false);
    mailbox.close();
  }

  Metrics snapshot_metrics() {
    std::lock_guard<std::mutex> lock(metrics_mu);
    metrics.proxy_p95_ms = latency.percentile(0.95);
    metrics.proxy_p99_ms = latency.percentile(0.99);
    metrics.proxy_samples = latency.samples();
    if (measured_frames >= 2) {
      const double seconds = std::chrono::duration<double>(fps_last - fps_origin).count();
      metrics.fps = seconds > 0 ? static_cast<double>(measured_frames - 1) / seconds : 0;
    } else {
      metrics.fps = 0;
    }
    return metrics;
  }

  void publish_metrics() { emit_line(metrics_event(snapshot_metrics())); }

  void open_input(const BuiltEndpoint& built, const Settings& settings) {
    AVFormatContext* context = avformat_alloc_context();
    if (context == nullptr) {
      throw std::runtime_error("немає контексту входу");
    }
    context->interrupt_callback.callback = interrupt_cb;
    context->interrupt_callback.opaque = &running;
    const bool live = settings.input.protocol != "file" && built.format != "sdp";
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
    for (const auto& option : built.protocol_options) {
      av_dict_set(&options, option.first.c_str(), option.second.c_str(), 0);
    }
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
    input_tb = stream->time_base;
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
    metrics.input_width = decoder->width;
    metrics.input_height = decoder->height;
    AVRational rate = stream->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) {
      rate = stream->r_frame_rate;
    }
    if (rate.num <= 0 || rate.den <= 0) {
      rate = AVRational{30, 1};
    }
    budget_ms = 1000.0 * static_cast<double>(rate.den) / static_cast<double>(rate.num);
  }

  void open_processor(const Settings& settings) {
    processor = create_frame_processor();
    if (!processor) {
      throw std::runtime_error("обробник RTX Video SDK не зібрано");
    }
    active_scale = settings.scale;
    if (!processor->open(hw_device, decoder->width, decoder->height, active_scale, quality.quality())) {
      const std::string first_error = processor->last_error();
      if (active_scale == 3 && quality.automatic() &&
          processor->open(hw_device, decoder->width, decoder->height, 2, quality.quality())) {
        active_scale = 2;
        std::cerr << "масштаб 3 недоступний, лишаю 2\n";
      } else {
        throw std::runtime_error(first_error);
      }
    }
  }

  AVFrame* make_scratch(int width, int height) {
    AVBufferRef* pool = av_hwframe_ctx_alloc(hw_device);
    if (pool == nullptr) {
      return nullptr;
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(pool->data);
    frames->format = AV_PIX_FMT_CUDA;
    frames->sw_format = AV_PIX_FMT_NV12;
    frames->width = width;
    frames->height = height;
    frames->initial_pool_size = 2;
    if (av_hwframe_ctx_init(pool) < 0) {
      av_buffer_unref(&pool);
      return nullptr;
    }
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr || av_hwframe_get_buffer(pool, frame, 0) < 0) {
      av_frame_free(&frame);
      av_buffer_unref(&pool);
      return nullptr;
    }
    av_buffer_unref(&pool);
    return frame;
  }

  void maybe_reduce_scale(AVFrame* frame) {
    if (active_scale != 3 || !quality.automatic() || frame == nullptr) {
      return;
    }
    AVFrame* scratch = make_scratch(processor->output_width(), processor->output_height());
    double process_ms = 0;
    const bool ok = scratch != nullptr && processor->process(frame, scratch, process_ms);
    av_frame_free(&scratch);
    if (!ok || process_ms <= budget_ms * 0.85) {
      return;
    }
    std::cerr << "масштаб 3 довший за бюджет кадру (" << process_ms << " мс), переходжу на 2\n";
    processor->close();
    if (!processor->open(hw_device, decoder->width, decoder->height, 2, quality.quality())) {
      throw std::runtime_error(processor->last_error());
    }
    active_scale = 2;
  }

  void open_encoder(const Settings& settings, BuiltEndpoint& built) {
    const int dst_w = processor->output_width();
    const int dst_h = processor->output_height();
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
    encoder->width = dst_w;
    encoder->height = dst_h;
    encoder->framerate = rate;
    encoder->time_base = av_inv_q(rate);
    encoder->pix_fmt = AV_PIX_FMT_CUDA;
    apply_color(encoder, choose_color(in_stream->codecpar->color_space, in_stream->codecpar->color_range, decoder->width,
                                      decoder->height));
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
    // Елементарний RTP. У rtp_mpegts тип 33 заданий кодеком MPEG-TS, окремий payload_type його ламає.
    if (built.format == "rtp") {
      const char* payload = settings.codec == "hevc" ? "98" : "96";
      const int payload_status = av_opt_set(output->priv_data, "payload_type", payload, 0);
      if (payload_status < 0) {
        throw std::runtime_error(std::string("payload_type: ") + ffmpeg_error(payload_status));
      }
    }
    if (!(output->oformat->flags & AVFMT_NOFILE)) {
      AVDictionary* io_options = to_dict(built.protocol_options);
      throw_if(avio_open2(&output->pb, built.ffmpeg_url.c_str(), AVIO_FLAG_WRITE, &output->interrupt_callback, &io_options),
               "відкриття виходу");
      ensure_consumed(io_options, "вихід");
    }
    throw_if(avformat_write_header(output, nullptr), "заголовок виходу");
    header_written = true;
    sdp_path = built.sdp_path;
    if (!sdp_path.empty()) {
      write_sdp();
    }
    metrics.output_width = dst_w;
    metrics.output_height = dst_h;
    metrics.vsr_quality = processor->quality();
    metrics.vsr_scale = active_scale;
    metrics.vsr_state = "running";
    measure_from = std::chrono::steady_clock::now();
    public_state = "running";
    emit_line(status_event("running", {}));
    publish_metrics();
  }

  void write_sdp() {
    char buffer[8192];
    AVFormatContext* contexts[] = {output};
    throw_if(av_sdp_create(contexts, 1, buffer, static_cast<int>(sizeof(buffer))), "SDP");
    const std::filesystem::path path(sdp_path);
    if (path.has_parent_path()) {
      std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
      throw std::runtime_error("не вдалося записати SDP");
    }
    file << buffer;
  }

  void write_packet(AVPacket* encoded) {
    const auto started = std::chrono::steady_clock::now();
    AVStream* stream = output->streams[0];
    av_packet_rescale_ts(encoded, encoder->time_base, stream->time_base);
    encoded->stream_index = stream->index;
    throw_if(av_interleaved_write_frame(output, encoded), "запис виходу");
    frame_write_ms += elapsed_ms(started, std::chrono::steady_clock::now());
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

  void stamp(AVFrame* frame, double decode_ms) {
    auto* timing = new FrameTiming;
    timing->decode_ms = decode_ms;
    timing->decoded_at = std::chrono::steady_clock::now();
    timing->ready_at = timing->decoded_at;
    frame->opaque = timing;
    std::lock_guard<std::mutex> lock(metrics_mu);
    ++metrics.frames_in;
  }

  bool next_video_frame(AVPacket* incoming, AVFrame* frame) {
    auto sent_at = std::chrono::steady_clock::now();
    bool sent = false;
    while (running.load()) {
      const int received = avcodec_receive_frame(decoder, frame);
      if (received == 0) {
        if (frame->format != AV_PIX_FMT_CUDA) {
          fail("декодер віддав кадр не в пам'яті GPU");
          return false;
        }
        const double decode_ms = sent ? elapsed_ms(sent_at, std::chrono::steady_clock::now()) : 0;
        stamp(frame, decode_ms);
        return true;
      }
      if (received != AVERROR(EAGAIN) && received != AVERROR_EOF) {
        fail("кадр NVDEC: " + ffmpeg_error(received));
        return false;
      }
      if (received == AVERROR_EOF) {
        return false;
      }
      const int read = av_read_frame(input, incoming);
      if (read == AVERROR_EOF || read == AVERROR_EXIT) {
        return false;
      }
      if (read < 0) {
        fail("читання входу: " + ffmpeg_error(read));
        return false;
      }
      if (incoming->stream_index != video_index) {
        av_packet_unref(incoming);
        continue;
      }
      sent_at = std::chrono::steady_clock::now();
      const int status = avcodec_send_packet(decoder, incoming);
      av_packet_unref(incoming);
      sent = true;
      if (status < 0 && status != AVERROR(EAGAIN)) {
        fail("NVDEC: " + ffmpeg_error(status));
        return false;
      }
    }
    return false;
  }

  AVFrame* pull_one_frame() {
    AVPacket* incoming = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (incoming == nullptr || frame == nullptr) {
      av_packet_free(&incoming);
      av_frame_free(&frame);
      fail("немає буфера декодера");
      return nullptr;
    }
    if (!next_video_frame(incoming, frame)) {
      av_packet_free(&incoming);
      av_frame_free(&frame);
      if (error.empty()) {
        fail("немає відеокадру на вході");
      }
      return nullptr;
    }
    av_packet_free(&incoming);
    AVFrame* owned = av_frame_alloc();
    if (owned == nullptr) {
      av_frame_free(&frame);
      fail("немає буфера кадру");
      return nullptr;
    }
    av_frame_move_ref(owned, frame);
    av_frame_free(&frame);
    return owned;
  }

  void pace_owned_frame(AVFrame* frame) {
    if (!pace_origin_set) {
      pace_origin = std::chrono::steady_clock::now();
      pace_origin_pts = frame->pts;
      pace_origin_set = true;
      return;
    }
    double delay_ms = budget_ms;
    if (frame->pts != AV_NOPTS_VALUE && pace_origin_pts != AV_NOPTS_VALUE && frame->pts > pace_origin_pts &&
        input_tb.den > 0) {
      delay_ms = av_q2d(input_tb) * static_cast<double>(frame->pts - pace_origin_pts) * 1000.0;
    }
    const auto target = pace_origin + std::chrono::duration<double, std::milli>(delay_ms);
    while (running.load() && std::chrono::steady_clock::now() < target) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (auto* timing = static_cast<FrameTiming*>(frame->opaque)) {
      timing->ready_at = std::chrono::steady_clock::now();
    }
  }

  void decode_loop() {
    AVPacket* incoming = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (incoming == nullptr || frame == nullptr) {
      fail("немає буфера декодера");
      av_packet_free(&incoming);
      av_frame_free(&frame);
      running.store(false);
      mailbox.close();
      return;
    }
    while (running.load()) {
      if (!next_video_frame(incoming, frame)) {
        break;
      }
      AVFrame* owned = av_frame_alloc();
      if (owned == nullptr) {
        fail("немає буфера кадру");
        break;
      }
      av_frame_move_ref(owned, frame);
      if (pace_input) {
        pace_owned_frame(owned);
      }
      mailbox.publish(owned);
    }
    av_packet_free(&incoming);
    av_frame_free(&frame);
    running.store(false);
    mailbox.close();
  }

  bool handle_frame(AVFrame* input_frame, const Settings& settings,
                    std::chrono::steady_clock::time_point& last_report) {
    auto* timing = static_cast<FrameTiming*>(input_frame->opaque);
    const auto taken = std::chrono::steady_clock::now();
    const double wait_ms = timing == nullptr ? 0 : proxy_queue_ms(timing->decoded_at, timing->ready_at, taken);
    const double decode_ms = timing == nullptr ? 0 : timing->decode_ms;
    AVFrame* output_frame = av_frame_alloc();
    if (output_frame == nullptr || av_hwframe_get_buffer(encoder->hw_frames_ctx, output_frame, 0) < 0) {
      free_frame(input_frame);
      av_frame_free(&output_frame);
      fail("немає вихідного кадру CUDA");
      return false;
    }
    output_frame->pts = map_pts(pts, input_frame->pts, input_tb, encoder->time_base);
    set_frame_duration(output_frame);
    double process_ms = 0;
    bool ok = processor->process(input_frame, output_frame, process_ms);
    const bool vsr_frame = ok && !processor->degraded();
    if (!ok) {
      std::cerr << "VSR: " << processor->last_error() << ", резервний масштаб\n";
      ok = processor->fallback(input_frame, output_frame);
    }
    free_frame(input_frame);
    if (!ok) {
      av_frame_free(&output_frame);
      fail(processor->last_error().empty() ? "резервний масштаб не зібрав кадр" : processor->last_error());
      return false;
    }
    if (processor->degraded() && !bypass_logged) {
      std::cerr << "VSR вимкнено, кадри йдуть без покращення\n";
      bypass_logged = true;
    }
    const auto encode_started = std::chrono::steady_clock::now();
    frame_write_ms = 0;
    int sent = AVERROR(EAGAIN);
    try {
      for (int attempt = 0; attempt < 3 && sent == AVERROR(EAGAIN); ++attempt) {
        if (attempt > 0) {
          drain_encoder();
        }
        sent = avcodec_send_frame(encoder, output_frame);
      }
      av_frame_free(&output_frame);
      if (sent < 0) {
        fail("NVENC: " + ffmpeg_error(sent));
        return false;
      }
      drain_encoder();
    } catch (...) {
      av_frame_free(&output_frame);
      throw;
    }
    const double encode_ms = elapsed_ms(encode_started, std::chrono::steady_clock::now());
    const double total_ms = decode_ms + wait_ms + process_ms + encode_ms;
    if (!processor->degraded()) {
      const bool changed = quality.observe(total_ms, budget_ms);
      processor->set_quality(quality.quality());
      if (changed) {
        std::cerr << "VSR quality -> " << quality.quality() << '\n';
      }
    }
    std::string state = "running";
    if (processor->degraded()) {
      state = "bypass";
    } else if (quality.quality() <= settings.quality_min && total_ms > budget_ms * 0.85) {
      state = "overloaded";
    } else if (quality.quality() < settings.quality_max) {
      state = "degraded";
    }
    {
      std::lock_guard<std::mutex> lock(metrics_mu);
      ++metrics.frames_out;
      metrics.frames_dropped = mailbox.dropped();
      metrics.last_process_ms = process_ms;
      metrics.avg_process_ms = metrics.avg_process_ms == 0 ? process_ms : metrics.avg_process_ms * 0.9 + process_ms * 0.1;
      metrics.last_decode_ms = decode_ms;
      metrics.last_wait_ms = wait_ms;
      metrics.last_encode_ms = encode_ms;
      metrics.last_write_ms = frame_write_ms;
      metrics.last_total_ms = total_ms;
      metrics.avg_total_ms = metrics.avg_total_ms == 0 ? total_ms : metrics.avg_total_ms * 0.9 + total_ms * 0.1;
      metrics.vsr_quality = processor->degraded() ? 0 : processor->quality();
      metrics.vsr_scale = active_scale;
      metrics.vsr_state = state;
      const bool warmed = warmup_s <= 0 || warmup_done;
      if (!warmed && std::chrono::steady_clock::now() - measure_from >= std::chrono::duration<double>(warmup_s)) {
        latency.clear();
        warmup_done = true;
      }
      if (warmup_s <= 0 || warmup_done) {
        const auto measured_at = std::chrono::steady_clock::now();
        if (!fps_origin_set) {
          fps_origin = measured_at;
          fps_origin_set = true;
        }
        fps_last = measured_at;
        ++measured_frames;
        latency.add(total_ms);
      }
      if (state == "overloaded") {
        ++metrics.overload_frames;
      }
      if (state == "bypass") {
        if (!saw_bypass) {
          ++metrics.bypass_entries;
          saw_bypass = true;
        }
      } else {
        saw_bypass = false;
      }
      if (vsr_frame) {
        ++metrics.vsr_frames;
      }
    }
    if (state != public_state) {
      public_state = state;
      emit_line(status_event(state, {}));
    }
    const auto now = std::chrono::steady_clock::now();
    if (settings.metrics_interval > 0 && now - last_report >= std::chrono::duration<double>(settings.metrics_interval)) {
      const Metrics snapshot = snapshot_metrics();
      emit_line(metrics_event(snapshot));
      if (!settings.metrics_file.empty() && !write_metrics_file(settings.metrics_file, snapshot)) {
        std::cerr << "не вдалося записати файл метрик\n";
      }
      last_report = now;
    }
    return true;
  }

  void process_loop(const Settings& settings) {
    packet = av_packet_alloc();
    auto last_report = std::chrono::steady_clock::now();
    while (true) {
      AVFrame* input_frame = mailbox.take(running, std::chrono::milliseconds(50));
      if (input_frame == nullptr) {
        if (!running.load()) {
          break;
        }
        continue;
      }
      try {
        if (!handle_frame(input_frame, settings, last_report)) {
          break;
        }
      } catch (const std::exception& ex) {
        fail(ex.what());
        break;
      }
    }
    if (error.empty() && encoder != nullptr) {
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

struct SignalGuard {
  ~SignalGuard() { g_running = nullptr; }
};

struct ControlReader {
  std::atomic<bool> stop{false};
  std::thread thread;
  ControlReader() {
    thread = std::thread([this] {
      std::string line;
      while (!stop.load()) {
        const int got = poll_stdin_line(line, 200);
        if (got < 0) {
          break;
        }
        if (got > 0 && line == "stop" && g_running != nullptr) {
          g_running->store(false);
        }
      }
    });
  }
  ~ControlReader() {
    stop.store(true);
    if (thread.joinable()) {
      thread.join();
    }
  }
};

std::string benchmark_event(const Metrics& metrics) {
  std::string body = metrics_event(metrics);
  const std::string from = "\"type\":\"metrics\"";
  const auto pos = body.find(from);
  if (pos != std::string::npos) {
    body.replace(pos, from.size(), "\"type\":\"benchmark\"");
  }
  const char* version = av_version_info();
  body.insert(body.size() - 1,
              std::string(",\"ok\":true,\"ffmpeg\":\"") + json_escape(version == nullptr ? "" : version) + "\"");
  return body;
}

}  // namespace

int run_proxy(const Settings& settings, const std::vector<Mode>& modes, const OptionProbe& probe, Metrics* summary) {
  g_emit_status = settings.status_events;
  const std::string denied_input = password_access_error(settings.input.url, settings.input.params, {});
  if (!denied_input.empty()) {
    std::cerr << denied_input << '\n';
    return 2;
  }
  const std::string denied_output = password_access_error(settings.output.url, settings.output.params, {});
  if (!denied_output.empty()) {
    std::cerr << denied_output << '\n';
    return 2;
  }
  const Mode* input_mode = find_mode(modes, Direction::Input, settings.input.protocol, settings.input.role,
                                     rtp_container(settings.input, Direction::Input));
  const Mode* output_mode = find_mode(modes, Direction::Output, settings.output.protocol, settings.output.role,
                                      rtp_container(settings.output, Direction::Output));
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
  Settings resolved = settings;
  const DependencyReport report = inspect_dependencies();
  const std::string missing_gpu = bind_gpu_selection(resolved, report);
  std::string blockers = dependency_blockers(resolved, report);
  if (!missing_gpu.empty()) {
    blockers += missing_gpu + "\n";
  }
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
    Session session(resolved);
    struct SummaryGuard {
      Session& session;
      Metrics* summary;
      ~SummaryGuard() {
        if (summary != nullptr) {
          *summary = session.snapshot_metrics();
        }
      }
    } summary_guard{session, summary};
    if (const GpuInfo* gpu = find_gpu(report.gpus, resolved.gpu)) {
      session.metrics.gpu_index = gpu->index;
      session.metrics.gpu_uuid = gpu->uuid;
      session.metrics.gpu_name = gpu->name;
    }
    g_running = &session.running;
    SignalGuard guard;
    ControlReader control;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    ignore_pipe_signal();
    emit_line(status_event("connecting", {}));
    session.open_input(input_built, resolved);
    session.open_decoder(resolved);
    session.open_processor(resolved);
    AVFrame* first = session.pull_one_frame();
    if (first == nullptr) {
      std::cerr << session.error << '\n';
      return 1;
    }
    try {
      session.maybe_reduce_scale(first);
    } catch (...) {
      free_frame(first);
      throw;
    }
    session.mailbox.publish(first);
    std::thread decoder([&session] { session.decode_loop(); });
    std::thread worker;
    try {
      if (resolved.output.role == "listener" || resolved.output.role == "server") {
        emit_line(status_event("waiting", {}));
      }
      session.open_encoder(resolved, output_built);
      worker = std::thread([&session, &resolved] { session.process_loop(resolved); });
      decoder.join();
      worker.join();
    } catch (...) {
      session.running.store(false);
      session.mailbox.close();
      if (decoder.joinable()) {
        decoder.join();
      }
      if (worker.joinable()) {
        worker.join();
      }
      throw;
    }
    session.publish_metrics();
    {
      std::lock_guard<std::mutex> lock(session.error_mu);
      if (!session.error.empty()) {
        std::cerr << session.error << '\n';
        return 1;
      }
    }
    if (resolved.pace_input) {
      emit_line(benchmark_event(session.snapshot_metrics()));
    }
    emit_line(status_event("stopped", {}));
    return 0;
  } catch (const std::exception& ex) {
    g_running = nullptr;
    std::cerr << ex.what() << '\n';
    return 1;
  }
}

}  // namespace rtx
