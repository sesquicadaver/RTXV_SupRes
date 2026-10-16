#include "check.hpp"
#include "cli.hpp"
#include "dependencies.hpp"
#include "endpoint.hpp"
#include "latency.hpp"
#include "profile.hpp"
#include "report.hpp"

#include <chrono>
#include <filesystem>

int main() {
  const rtx::UrlSecret parts = rtx::split_url_secret("rtsp://user:secret@192.168.1.20:554/live");
  CHECK(parts.has_userinfo);
  CHECK(parts.has_password);
  CHECK(parts.user == "user");
  CHECK(parts.url == "rtsp://192.168.1.20:554/live");
  const rtx::UrlSecret named = rtx::split_url_secret("rtsp://user@192.168.1.20:554/live");
  CHECK(named.has_userinfo);
  CHECK(!named.has_password);
  CHECK(named.user == "user");
  CHECK(rtx::password_access_error("rtsp://192.168.1.20:554/live", {}, {}).empty());
  CHECK(rtx::password_access_error("rtsp://user@192.168.1.20:554/live", {}, {}).find("адресу і порт") !=
        std::string::npos);
  CHECK(rtx::password_access_error("rtsp://user:secret@192.168.1.20:554/live", {}, {}).find("адресу і порт") !=
        std::string::npos);
  CHECK(rtx::password_access_error("srt://10.0.0.1:9000?passphrase=hunter2&mode=caller", {}, {}).find("адресу і порт") !=
        std::string::npos);
  std::vector<std::pair<std::string, std::string>> params{{"passphrase", "xyz"}};
  CHECK(rtx::password_access_error("srt://10.0.0.1:9000?mode=caller", params, {}).find("адресу і порт") !=
        std::string::npos);
  CHECK(rtx::password_access_error("srt://10.0.0.1:9000?mode=caller", {}, "profile/old").find("адресу і порт") !=
        std::string::npos);

  rtx::Profile profile;
  profile.name = "FPV";
  profile.input.protocol = "rtsp";
  profile.input.role = "client";
  profile.input.url = "rtsp://192.168.1.20:554/live";
  std::string error;
  CHECK(rtx::password_access_error(profile).empty());
  profile.input.url = "rtsp://user@192.168.1.20:554/live";
  CHECK(rtx::password_access_error(profile).find("адресу і порт") != std::string::npos);
  profile.input.url = "rtsp://192.168.1.20:554/live";

  rtx::Profile fields;
  fields.output.protocol = "rtp";
  fields.output.role = "client";
  fields.output.remote_host = "10.0.0.5";
  fields.output.remote_port = 5004;
  fields.output.local_port = 5006;
  fields.output.sdp = "out.sdp";
  fields.output.app = "live";
  fields.output.playpath = "cam";
  fields.output.rtcp_port = 5005;
  fields.codec = "h264";
  const std::string fields_json = rtx::profile_json(fields);
  rtx::Profile fields_loaded;
  CHECK(rtx::parse_profile(fields_json, fields_loaded, error));
  CHECK(fields_loaded.output.sdp == "out.sdp");
  CHECK(fields_loaded.output.app == "live");
  CHECK(fields_loaded.output.playpath == "cam");
  CHECK(fields_loaded.output.local_port == 5006);
  CHECK(fields_loaded.output.remote_port == 5004);
  CHECK(fields_loaded.output.rtcp_port == 5005);

  rtx::Profile rejected;
  CHECK(!rtx::parse_profile("{\"schema\":99}", rejected, error));

  const auto path = std::filesystem::temp_directory_path() / "rtx-profile-roundtrip.json";
  profile.gpu_uuid = "GPU-abc";
  profile.scale = 3;
  CHECK(rtx::save_profile_file(path, profile, error));
  rtx::Profile from_disk;
  CHECK(rtx::load_profile_file(path, from_disk, error));
  CHECK(from_disk.gpu_uuid == "GPU-abc");
  CHECK(from_disk.scale == 3);
  CHECK(from_disk.input.url.find("secret") == std::string::npos);
  std::filesystem::remove(path);

  rtx::DependencyReport report;
  report.sdk_built = true;
  report.nvenc_h264 = true;
  rtx::GpuInfo gpu;
  gpu.index = 2;
  gpu.name = "Card";
  gpu.uuid = "GPU-abc";
  gpu.vram_bytes = 1024;
  report.gpus.push_back(gpu);
  rtx::Settings settings;
  settings.gpu = 2;
  settings.codec = "h264";
  CHECK(rtx::gpu_list_blocker(settings, report).empty());
  settings.gpu = 0;
  CHECK(rtx::gpu_list_blocker(settings, report).find("відсутній") != std::string::npos);
  settings.gpu = 2;
  settings.gpu_uuid = "GPU-missing";
  CHECK(!rtx::bind_gpu_selection(settings, report).empty());
  settings.gpu_uuid = "GPU-abc";
  CHECK(rtx::bind_gpu_selection(settings, report).empty());
  CHECK(settings.gpu == 2);

  report.sdk_built = false;
  report.sdk_note = "немає SDK";
  const std::string doctor = rtx::doctor_json(report, rtx::dependency_blockers(settings, report), 3);
  CHECK(doctor.find("\"ok\":false") != std::string::npos);
  CHECK(doctor.find("\"effect\":\"unconfirmed\"") != std::string::npos);
  CHECK(doctor.find("GPU-abc") != std::string::npos);
  CHECK(doctor.find("\"index\":2") != std::string::npos);

  rtx::ProxyLatency latency;
  for (int value = 1; value <= 100; ++value) {
    latency.add(value);
  }
  CHECK(latency.samples() == 100);
  CHECK(latency.percentile(0.95) >= 95);
  CHECK(latency.percentile(0.99) >= 99);

  rtx::Endpoint endpoint;
  endpoint.protocol = "rtp";
  endpoint.role = "client";
  endpoint.remote_host = "10.0.0.5";
  endpoint.remote_port = 5004;
  CHECK(rtx::consumer_address(endpoint) == "rtp://10.0.0.5:5004");
  CHECK(rtx::port_caption(rtx::Direction::Input, rtx::Role::Listener) == "локальний порт прийому");
  CHECK(rtx::port_caption(rtx::Direction::Output, rtx::Role::Client) == "порт отримувача");
  CHECK(rtx::port_caption(rtx::Direction::Output, rtx::Role::Server) == "локальний порт сервера");
  rtx::Endpoint hls;
  hls.protocol = "hls";
  hls.role = "server";
  hls.local_host = "127.0.0.1";
  hls.local_port = 8080;
  CHECK(rtx::consumer_address(hls) == "http://127.0.0.1:8080/index.m3u8");

  const auto decoded = std::chrono::steady_clock::time_point{};
  const auto ready = decoded + std::chrono::milliseconds(100);
  const auto taken = decoded + std::chrono::milliseconds(110);
  CHECK(rtx::proxy_queue_ms(decoded, ready, taken) < 20.0);
  CHECK(rtx::proxy_queue_ms(decoded, decoded, taken) > 100.0);
  CHECK(!rtx::measurement_within_budget(0, 0, 33));
  CHECK(rtx::measurement_within_budget(8, 20, 33));
  CHECK(!rtx::measurement_within_budget(8, 40, 33));
  CHECK(rtx::benchmark_run_valid(0, true, 8));
  CHECK(!rtx::benchmark_run_valid(1, true, 8));
  CHECK(!rtx::benchmark_run_valid(0, false, 8));
  CHECK(!rtx::benchmark_run_valid(0, true, 0));

  rtx::Endpoint elementary;
  elementary.protocol = "rtp";
  elementary.role = "client";
  elementary.payload = "es";
  elementary.path = "in.sdp";
  rtx::Endpoint mpegts_rtp = elementary;
  mpegts_rtp.payload.clear();
  mpegts_rtp.role = "listener";
  CHECK(rtx::rtp_container(elementary, rtx::Direction::Input) == "sdp");
  CHECK(rtx::rtp_container(mpegts_rtp, rtx::Direction::Input) == "rtp");
  std::vector<rtx::Mode> rtp_modes(2);
  rtp_modes[0].protocol = "rtp";
  rtp_modes[0].direction = rtx::Direction::Input;
  rtp_modes[0].role = rtx::Role::Client;
  rtp_modes[0].container = "sdp";
  rtp_modes[1].protocol = "rtp";
  rtp_modes[1].direction = rtx::Direction::Input;
  rtp_modes[1].role = rtx::Role::Listener;
  rtp_modes[1].container = "rtp";
  CHECK(rtx::find_mode(rtp_modes, rtx::Direction::Input, "rtp", "client",
                       rtx::rtp_container(elementary, rtx::Direction::Input)) != nullptr);
  CHECK(rtx::find_mode(rtp_modes, rtx::Direction::Input, "rtp", "client",
                       rtx::rtp_container(mpegts_rtp, rtx::Direction::Input)) == nullptr);
  CHECK(rtx::find_mode(rtp_modes, rtx::Direction::Input, "rtp", "listener",
                       rtx::rtp_container(mpegts_rtp, rtx::Direction::Input)) != nullptr);

  rtx::Endpoint saved;
  saved.protocol = "rtsp";
  saved.role = "client";
  saved.url = "rtsp://192.168.1.20:554/live";
  std::string overlay_error;
  CHECK(rtx::overlay_endpoint_flags(saved, {{"local-port", "5600"}}, overlay_error));
  CHECK(saved.protocol == "rtsp");
  CHECK(saved.role == "client");
  CHECK(saved.url == "rtsp://192.168.1.20:554/live");
  CHECK(saved.local_port == 5600);
  CHECK(!rtx::overlay_endpoint_flags(saved, {{"remote-port", "abc"}}, overlay_error));
  return rtx_test::result();
}
