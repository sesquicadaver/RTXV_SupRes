#include "processor.hpp"

#include "gpu_probe.hpp"
#include "types.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

#if defined(RTX_WITH_SDK)
#include "gpu/rtx_processor.hpp"
#endif

namespace rtx {

std::unique_ptr<FrameProcessor> create_frame_processor() {
#if defined(RTX_WITH_SDK)
  return create_rtx_processor();
#else
  return nullptr;
#endif
}

DependencyReport inspect_dependencies() {
  DependencyReport report;
  report.gpus = probe_cuda_devices(report.cuda_note);
  report.nvenc_h264 = avcodec_find_encoder_by_name("h264_nvenc") != nullptr;
  report.nvenc_hevc = avcodec_find_encoder_by_name("hevc_nvenc") != nullptr;
#if defined(RTX_SDK_COMPILE_CHECK)
  report.sdk_built = false;
  report.sdk_note =
      "зібрано перевірку компіляції CUDA без NVIDIA RTX Video SDK; ефект VSR цим бінарником не підтверджено";
#elif defined(RTX_WITH_SDK)
  report.sdk_built = true;
  report.sdk_note = "RTX Video SDK зібрано в бінарник";
#else
  report.sdk_built = false;
  report.sdk_note =
      "бінарник зібрано без RTX Video SDK; на цільовій машині потрібні CUDA Toolkit 12.x і RTX_VIDEO_SDK_ROOT";
#endif
  return report;
}

std::string dependency_blockers(const Settings& settings, const DependencyReport& report) {
  std::string text;
  if (!report.sdk_built) {
    text += report.sdk_note + "\n";
  }
  if (report.gpus.empty()) {
    text += report.cuda_note + "\n";
  } else if (settings.gpu < 0 || settings.gpu >= static_cast<int>(report.gpus.size())) {
    text += "індекс GPU поза межами виявлених пристроїв\n";
  }
  if (settings.codec == "h264" && !report.nvenc_h264) {
    text += "у цій збірці FFmpeg немає кодера h264_nvenc\n";
  }
  if (settings.codec == "hevc" && !report.nvenc_hevc) {
    text += "у цій збірці FFmpeg немає кодера hevc_nvenc\n";
  }
#if defined(RTX_WITH_SDK)
  if (text.empty()) {
    const std::string sdk_error = probe_rtx_sdk(settings.gpu);
    if (!sdk_error.empty()) {
      text += sdk_error + "\n";
    }
  }
#endif
  return text;
}

}  // namespace rtx
