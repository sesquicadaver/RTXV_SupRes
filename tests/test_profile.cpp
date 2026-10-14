#include "check.hpp"
#include "dependencies.hpp"
#include "endpoint.hpp"
#include "latency.hpp"
#include "profile.hpp"
#include "report.hpp"

#include <filesystem>
#include <map>

namespace {

class MemoryStore final : public rtx::SecretStore {
 public:
  bool save(const std::string& id, const std::string& secret, std::string& error) override {
    secrets[id] = secret;
    error.clear();
    return true;
  }
  bool load(const std::string& id, std::string& secret, std::string& error) override {
    const auto found = secrets.find(id);
    if (found == secrets.end()) {
      error = "немає";
      return false;
    }
    secret = found->second;
    error.clear();
    return true;
  }
  bool remove(const std::string& id, std::string& error) override {
    secrets.erase(id);
    error.clear();
    return true;
  }
  std::map<std::string, std::string> secrets;
};

}  // namespace

int main() {
  const rtx::UrlSecret parts = rtx::split_url_secret("rtsp://user:secret@192.168.1.20:554/live");
  CHECK(parts.has_password);
  CHECK(parts.user == "user");
  CHECK(parts.password == "secret");
  CHECK(parts.url == "rtsp://192.168.1.20:554/live");
  CHECK(rtx::join_url_secret(parts.url, parts.user, parts.password) == "rtsp://user:secret@192.168.1.20:554/live");

  rtx::Profile profile;
  profile.name = "FPV";
  profile.input.protocol = "rtsp";
  profile.input.role = "client";
  MemoryStore store;
  std::string error;
  CHECK(rtx::seal_input_secret(profile, "rtsp://user:secret@192.168.1.20:554/live", "profile/fpv", store, error));
  const std::string json = rtx::profile_json(profile);
  CHECK(json.find("secret") == std::string::npos);
  CHECK(json.find("credential_id") != std::string::npos);
  CHECK(store.secrets["profile/fpv"] == "secret");
  rtx::Profile loaded;
  CHECK(rtx::parse_profile(json, loaded, error));
  CHECK(rtx::open_input_secret(loaded, store, error));
  CHECK(loaded.input.url == "rtsp://user:secret@192.168.1.20:554/live");

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
  CHECK(rtx::dependency_blockers(settings, report).empty());
  settings.gpu = 0;
  CHECK(rtx::dependency_blockers(settings, report).find("відсутній") != std::string::npos);
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
  return rtx_test::result();
}
