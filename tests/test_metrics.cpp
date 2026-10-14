#include "metrics.hpp"

#include <cassert>
#include <string>

int main() {
  rtx::Metrics metrics;
  metrics.fps = 24.5;
  metrics.frames_in = 10;
  metrics.frames_out = 8;
  metrics.frames_dropped = 2;
  metrics.last_process_ms = 6.25;
  metrics.avg_process_ms = 7.5;
  metrics.vsr_quality = 2;
  metrics.vsr_scale = 2;
  metrics.vsr_state = "degraded";
  const std::string json = rtx::metrics_json(metrics);
  assert(json.find("\"frames_dropped\":2") != std::string::npos);
  assert(json.find("\"vsr_state\":\"degraded\"") != std::string::npos);
  assert(json.find("\"vsr_quality\":2") != std::string::npos);
  return 0;
}
