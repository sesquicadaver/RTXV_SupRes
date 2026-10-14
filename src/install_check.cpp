#include "install_check.hpp"

#include "catalog.hpp"
#include "dependencies.hpp"
#include "json_mini.hpp"
#include "pipeline.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace rtx {
namespace {

std::string safe_component(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const unsigned char symbol : text) {
    const bool ok = (symbol >= '0' && symbol <= '9') || (symbol >= 'A' && symbol <= 'Z') ||
                    (symbol >= 'a' && symbol <= 'z') || symbol == '-' || symbol == '_';
    out.push_back(ok ? static_cast<char>(symbol) : '_');
  }
  return out.empty() ? "gpu" : out;
}

}  // namespace

bool install_sample_ok(int exit_code, const SampleView& view, std::uint64_t vsr_frames, std::uint64_t bypass_entries) {
  return exit_code == 0 && view.frames > 0 && view.width > 0 && view.height > 0 && vsr_frames > 0 && bypass_entries == 0;
}

SampleView count_video_frames(const std::string& path, std::string& error) {
  SampleView view;
  AVFormatContext* format = nullptr;
  if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
    error = "вихідний файл не відкривається";
    return view;
  }
  if (avformat_find_stream_info(format, nullptr) < 0) {
    avformat_close_input(&format);
    error = "немає інформації про потік";
    return view;
  }
  const int stream_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    avformat_close_input(&format);
    error = "немає відеопотоку";
    return view;
  }
  AVCodecParameters* parameters = format->streams[stream_index]->codecpar;
  const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
  AVCodecContext* codec = decoder == nullptr ? nullptr : avcodec_alloc_context3(decoder);
  if (codec == nullptr || avcodec_parameters_to_context(codec, parameters) < 0 ||
      avcodec_open2(codec, decoder, nullptr) < 0) {
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    error = "декодер вихідного файлу не відкрився";
    return view;
  }
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  if (packet == nullptr || frame == nullptr) {
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    error = "немає пам'яті для перевірки кадру";
    return view;
  }
  auto take = [&]() {
    while (avcodec_receive_frame(codec, frame) == 0) {
      view.frames += 1;
      view.width = frame->width;
      view.height = frame->height;
      av_frame_unref(frame);
    }
  };
  while (av_read_frame(format, packet) >= 0) {
    if (packet->stream_index == stream_index && avcodec_send_packet(codec, packet) == 0) {
      take();
    }
    av_packet_unref(packet);
  }
  if (avcodec_send_packet(codec, nullptr) == 0) {
    take();
  }
  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&codec);
  avformat_close_input(&format);
  if (view.frames == 0 && error.empty()) {
    error = "у вихідному файлі немає кадру";
  }
  return view;
}

int run_install_check(const std::string& clip, const std::string& directory) {
  if (clip.empty()) {
    std::cerr << "install-check потребує --clip файл.h264\n";
    return 2;
  }
  std::error_code exists_error;
  if (!std::filesystem::is_regular_file(clip, exists_error) || exists_error) {
    std::cerr << "тестовий запис не знайдено\n";
    return 2;
  }
  const DependencyReport report = inspect_dependencies();
  const std::filesystem::path root = directory.empty()
                                         ? std::filesystem::temp_directory_path() / "rtx-vsr-install-check"
                                         : std::filesystem::path(directory);
  std::error_code directory_error;
  std::filesystem::create_directories(root, directory_error);
  const std::vector<Mode> modes = discover_modes();
  std::string failure;
  if (directory_error) {
    failure = "каталог перевірки не створено";
  } else if (!report.sdk_built) {
    failure = report.sdk_note.empty() ? "ефект RTX VSR: не підтверджено" : report.sdk_note;
  } else if (report.gpus.empty()) {
    failure = report.cuda_note.empty() ? "немає GPU" : report.cuda_note;
  }
  std::ostringstream gpus;
  bool all_ok = failure.empty();
  if (report.sdk_built && !directory_error) {
    for (std::size_t index = 0; index < report.gpus.size(); ++index) {
      const GpuInfo& gpu = report.gpus[index];
      Settings settings;
      settings.status_events = false;
      settings.input.protocol = "file";
      settings.input.role = "client";
      settings.input.path = clip;
      settings.output.protocol = "file";
      settings.output.role = "client";
      settings.output.path =
          (root / ("check-" + safe_component(gpu.uuid.empty() ? std::to_string(gpu.index) : gpu.uuid) + ".ts"))
              .string();
      settings.output_format = "mpegts";
      settings.codec = "h264";
      settings.scale = 2;
      settings.quality_auto = false;
      settings.quality = 1;
      settings.quality_min = 1;
      settings.quality_max = 1;
      settings.metrics_interval = 0;
      settings.gpu = gpu.index;
      settings.gpu_uuid = gpu.uuid;
      Metrics summary;
      const int exit_code = run_proxy(settings, modes, ffmpeg_probe(), &summary);
      std::string frame_error;
      const SampleView view = exit_code == 0 ? count_video_frames(settings.output.path, frame_error) : SampleView{};
      const bool ok = install_sample_ok(exit_code, view, summary.vsr_frames, summary.bypass_entries);
      if (!ok) {
        all_ok = false;
        if (failure.empty()) {
          if (view.frames > 0 && (summary.vsr_frames == 0 || summary.bypass_entries > 0)) {
            failure = "VSR не підтверджено або сеанс пішов у bypass";
          } else {
            failure = frame_error.empty() ? "тест конвеєра не дав кадру" : frame_error;
          }
        }
      }
      if (index != 0) {
        gpus << ',';
      }
      gpus << "{\"uuid\":\"" << json_escape(gpu.uuid) << "\",\"index\":" << gpu.index << ",\"exit_code\":" << exit_code
           << ",\"frames\":" << view.frames << ",\"width\":" << view.width << ",\"height\":" << view.height
           << ",\"vsr_frames\":" << summary.vsr_frames << ",\"bypass_entries\":" << summary.bypass_entries
           << ",\"ok\":" << (ok ? "true" : "false") << ",\"error\":\"" << json_escape(frame_error) << "\"}";
    }
  }
  std::cout << "{\"ok\":" << (all_ok ? "true" : "false") << ",\"gpus\":[" << gpus.str() << "],\"error\":\""
            << json_escape(failure) << "\"}\n";
  if (!report.sdk_built || report.gpus.empty()) {
    return 3;
  }
  return all_ok ? 0 : 1;
}

}  // namespace rtx
