#include "metrics.hpp"
#include "check.hpp"

#include <filesystem>
#include <fstream>
#include <string>

int main() {
  rtx::Metrics metrics;
  metrics.fps = 24.5;
  metrics.frames_in = 10;
  metrics.frames_out = 8;
  metrics.frames_dropped = 2;
  metrics.last_process_ms = 6.25;
  metrics.avg_process_ms = 7.5;
  metrics.last_total_ms = 12.5;
  metrics.vsr_quality = 2;
  metrics.vsr_scale = 2;
  metrics.vsr_state = "degraded";
  const std::string json = rtx::metrics_json(metrics);
  CHECK(json.find("\"frames_dropped\":2") != std::string::npos);
  CHECK(json.find("\"vsr_state\":\"degraded\"") != std::string::npos);
  CHECK(json.find("\"vsr_quality\":2") != std::string::npos);
  CHECK(json.find("\"last_total_ms\":12.50") != std::string::npos);
  CHECK(json.find("\"proxy_p95_ms\"") != std::string::npos);
  CHECK(json.find("\"proxy_p99_ms\"") != std::string::npos);

  const auto path = std::filesystem::temp_directory_path() / "rtx-metrics-replace.json";
  std::filesystem::remove(path);
  rtx::Metrics first;
  first.frames_in = 1;
  CHECK(rtx::write_metrics_file(path.string(), first));
  rtx::Metrics second;
  second.frames_in = 2;
  CHECK(rtx::write_metrics_file(path.string(), second));
  std::ifstream written(path);
  std::string body;
  std::getline(written, body);
  CHECK(body.find("\"frames_in\":2") != std::string::npos);
  written.close();
  std::filesystem::remove(path);
  return rtx_test::result();
}
