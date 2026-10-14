#include "report.hpp"

#include "json_mini.hpp"
#include "metrics.hpp"

#include <sstream>

namespace rtx {
namespace {

std::string blocker_array(const std::string& blockers) {
  std::ostringstream out;
  out << '[';
  bool first = true;
  std::size_t start = 0;
  while (start < blockers.size()) {
    std::size_t end = blockers.find('\n', start);
    if (end == std::string::npos) {
      end = blockers.size();
    }
    const std::string line = blockers.substr(start, end - start);
    if (!line.empty()) {
      if (!first) {
        out << ',';
      }
      first = false;
      out << '"' << json_escape(line) << '"';
    }
    start = end + 1;
  }
  out << ']';
  return out.str();
}

}  // namespace

const GpuInfo* find_gpu(const std::vector<GpuInfo>& gpus, int index) {
  for (const GpuInfo& gpu : gpus) {
    if (gpu.index == index) {
      return &gpu;
    }
  }
  return nullptr;
}

const GpuInfo* find_gpu_uuid(const std::vector<GpuInfo>& gpus, const std::string& uuid) {
  if (uuid.empty()) {
    return nullptr;
  }
  for (const GpuInfo& gpu : gpus) {
    if (gpu.uuid == uuid) {
      return &gpu;
    }
  }
  return nullptr;
}

std::string bind_gpu_selection(Settings& settings, const DependencyReport& report) {
  if (settings.gpu_uuid.empty()) {
    return {};
  }
  const GpuInfo* gpu = find_gpu_uuid(report.gpus, settings.gpu_uuid);
  if (gpu == nullptr) {
    return "збережений GPU відсутній, оберіть інший";
  }
  settings.gpu = gpu->index;
  return {};
}

std::string doctor_json(const DependencyReport& report, const std::string& blockers, std::size_t mode_count) {
  std::ostringstream out;
  const bool ok = blockers.empty();
  out << "{\"ok\":" << (ok ? "true" : "false") << ",\"sdk_built\":" << (report.sdk_built ? "true" : "false")
      << ",\"sdk_note\":\"" << json_escape(report.sdk_note) << "\",\"cuda_note\":\"" << json_escape(report.cuda_note)
      << "\",\"nvenc_h264\":" << (report.nvenc_h264 ? "true" : "false")
      << ",\"nvenc_hevc\":" << (report.nvenc_hevc ? "true" : "false") << ",\"effect\":\""
      << (ok ? "confirmed" : "unconfirmed") << "\",\"modes\":" << mode_count << ",\"blockers\":" << blocker_array(blockers)
      << ",\"gpus\":[";
  for (std::size_t i = 0; i < report.gpus.size(); ++i) {
    const GpuInfo& gpu = report.gpus[i];
    if (i != 0) {
      out << ',';
    }
    out << "{\"index\":" << gpu.index << ",\"name\":\"" << json_escape(gpu.name) << "\",\"uuid\":\""
        << json_escape(gpu.uuid) << "\",\"vram_bytes\":" << gpu.vram_bytes << ",\"cc_major\":" << gpu.cc_major
        << ",\"cc_minor\":" << gpu.cc_minor << "}";
  }
  out << "]}";
  return out.str();
}

std::string status_event(const std::string& state, const std::string& detail) {
  return std::string("{\"type\":\"status\",\"state\":\"") + json_escape(state) + "\",\"detail\":\"" +
         json_escape(detail) + "\"}";
}

std::string metrics_event(const Metrics& metrics) {
  std::string body = metrics_json(metrics);
  body.insert(1, "\"type\":\"metrics\",");
  return body;
}

}  // namespace rtx
