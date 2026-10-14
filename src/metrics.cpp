#include "metrics.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace rtx {
namespace {

std::string escape_json(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += ch;
        break;
    }
  }
  return out;
}

}  // namespace

std::string metrics_json(const Metrics& metrics) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(2);
  out << "{\"fps\":" << metrics.fps << ",\"frames_in\":" << metrics.frames_in
      << ",\"frames_out\":" << metrics.frames_out << ",\"frames_dropped\":" << metrics.frames_dropped
      << ",\"last_process_ms\":" << metrics.last_process_ms
      << ",\"avg_process_ms\":" << metrics.avg_process_ms
      << ",\"last_decode_ms\":" << metrics.last_decode_ms
      << ",\"last_wait_ms\":" << metrics.last_wait_ms
      << ",\"last_encode_ms\":" << metrics.last_encode_ms
      << ",\"last_write_ms\":" << metrics.last_write_ms
      << ",\"last_total_ms\":" << metrics.last_total_ms
      << ",\"avg_total_ms\":" << metrics.avg_total_ms
      << ",\"vsr_quality\":" << metrics.vsr_quality << ",\"vsr_scale\":" << metrics.vsr_scale
      << ",\"vsr_state\":\"" << escape_json(metrics.vsr_state) << "\"}";
  return out.str();
}

bool write_metrics_file(const std::string& path, const Metrics& metrics) {
  if (path.empty()) {
    return true;
  }
  const std::string tmp = path + ".tmp";
  {
    std::ofstream file(tmp, std::ios::trunc);
    if (!file) {
      return false;
    }
    file << metrics_json(metrics) << '\n';
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

}  // namespace rtx
