#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rtx {

enum class Direction { Input, Output };

enum class Role { Client, Listener, Server, Rendezvous };

struct Endpoint {
  std::string protocol;
  std::string role;
  std::string url;
  std::string path;
  std::string remote_host;
  int remote_port = -1;
  std::string local_host;
  int local_port = -1;
  std::string app;
  std::string playpath;
  int rtp_port_min = -1;
  int rtp_port_max = -1;
  int rtcp_port = -1;
  std::string directory;
  std::string payload;
  std::string sdp;
  std::vector<std::pair<std::string, std::string>> params;
};

struct Mode {
  std::string protocol;
  Direction direction = Direction::Input;
  Role role = Role::Client;
  std::string provider;
  std::string ffmpeg_name;
  std::string container;
  std::vector<std::string> required;
  std::vector<std::string> optional;
  bool url_satisfies_location = false;
  std::string summary;
};

struct BuiltEndpoint {
  std::string ffmpeg_url;
  std::string public_url;
  std::string format;
  std::map<std::string, std::string> format_options;
  std::map<std::string, std::string> protocol_options;
  std::string sdp_path;
  bool application_hls = false;
  std::string serve_host;
  int serve_port = -1;
  std::string serve_root;
};

struct Settings {
  bool json = false;
  bool verbose = false;
  Endpoint input;
  Endpoint output;
  int scale = 2;
  bool quality_auto = true;
  int quality = 2;
  int quality_min = 1;
  int quality_max = 4;
  int gpu = 0;
  std::string codec = "h264";
  std::int64_t bitrate = 0;
  double metrics_interval = 1.0;
  std::string metrics_file;
  std::string input_format;
  std::string output_format;
  std::string gpu_uuid;
  bool pace_input = false;
  double warmup_s = 0;
  bool status_events = true;
};

struct GpuInfo {
  int index = 0;
  std::string name;
  int cc_major = 0;
  int cc_minor = 0;
  std::string uuid;
  std::uint64_t vram_bytes = 0;
};

struct DependencyReport {
  bool sdk_built = false;
  std::string sdk_note;
  bool nvenc_h264 = false;
  bool nvenc_hevc = false;
  std::vector<GpuInfo> gpus;
  std::string cuda_note;
};

struct Metrics {
  double fps = 0;
  std::uint64_t frames_in = 0;
  std::uint64_t frames_out = 0;
  std::uint64_t frames_dropped = 0;
  double last_process_ms = 0;
  double avg_process_ms = 0;
  double last_decode_ms = 0;
  double last_wait_ms = 0;
  double last_encode_ms = 0;
  double last_write_ms = 0;
  double last_total_ms = 0;
  double avg_total_ms = 0;
  int vsr_quality = 0;
  int vsr_scale = 0;
  std::string vsr_state;
  double proxy_p95_ms = 0;
  double proxy_p99_ms = 0;
  std::uint64_t proxy_samples = 0;
  std::uint64_t overload_frames = 0;
  std::uint64_t bypass_entries = 0;
  int input_width = 0;
  int input_height = 0;
  int output_width = 0;
  int output_height = 0;
  int gpu_index = -1;
  std::string gpu_uuid;
  std::string gpu_name;
};

std::string to_string(Direction direction);
std::string to_string(Role role);
bool parse_role(const std::string& text, Role& role);
bool parse_direction(const std::string& text, Direction& direction);

}  // namespace rtx
