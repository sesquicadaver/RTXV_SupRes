#include "metrics.hpp"

#include "json_mini.hpp"
#include "path_utf8.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rtx {
namespace {

bool flush_path(const std::filesystem::path& path) {
#ifdef _WIN32
  const HANDLE handle =
      CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  const BOOL ok = FlushFileBuffers(handle);
  CloseHandle(handle);
  return ok != 0;
#else
  const int fd = ::open(path.c_str(), O_RDWR);
  if (fd < 0) {
    return false;
  }
  const int ok = ::fsync(fd);
  ::close(fd);
  return ok == 0;
#endif
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
      << ",\"vsr_state\":\"" << json_escape(metrics.vsr_state) << "\",\"proxy_p95_ms\":" << metrics.proxy_p95_ms
      << ",\"proxy_p99_ms\":" << metrics.proxy_p99_ms << ",\"proxy_samples\":" << metrics.proxy_samples
      << ",\"overload_frames\":" << metrics.overload_frames << ",\"bypass_entries\":" << metrics.bypass_entries
      << ",\"vsr_frames\":" << metrics.vsr_frames
      << ",\"input_width\":" << metrics.input_width << ",\"input_height\":" << metrics.input_height
      << ",\"output_width\":" << metrics.output_width << ",\"output_height\":" << metrics.output_height
      << ",\"gpu_index\":" << metrics.gpu_index << ",\"gpu_uuid\":\"" << json_escape(metrics.gpu_uuid)
      << "\",\"gpu_name\":\"" << json_escape(metrics.gpu_name) << "\"}";
  return out.str();
}

bool write_metrics_file(const std::string& path, const Metrics& metrics) {
  if (path.empty()) {
    return true;
  }
  const std::filesystem::path target = path_from_utf8(path);
  std::filesystem::path temporary = target;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) {
      return false;
    }
    file << metrics_json(metrics) << '\n';
    file.flush();
    if (!file) {
      return false;
    }
  }
  flush_path(temporary);
#ifdef _WIN32
  // Microsoft CRT: std::rename не замінює вже наявний файл призначення.
  const BOOL replaced = MoveFileExW(temporary.c_str(), target.c_str(),
                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (replaced == 0) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
#else
  std::error_code failure;
  std::filesystem::rename(temporary, target, failure);
  if (failure) {
    std::filesystem::remove(temporary, failure);
    return false;
  }
#endif
  flush_path(target);
  return true;
}

}  // namespace rtx
