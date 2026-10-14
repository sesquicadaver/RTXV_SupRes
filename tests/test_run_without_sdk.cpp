#include "catalog.hpp"
#include "check.hpp"
#include "pipeline.hpp"

#include <cstdio>
#include <fstream>

int main() {
  const char* path = "rtx-vsr-proxy-run-test.bin";
  {
    std::ofstream file(path);
    file << "not-a-video";
  }
  rtx::Settings settings;
  settings.input.protocol = "file";
  settings.input.role = "client";
  settings.input.path = path;
  settings.output.protocol = "udp";
  settings.output.role = "client";
  settings.output.remote_host = "127.0.0.1";
  settings.output.remote_port = 5600;
  settings.scale = 2;
  const int code = rtx::run_proxy(settings, rtx::discover_modes(), rtx::ffmpeg_probe());
  CHECK(code == 3);
  std::remove(path);
  return rtx_test::result();
}
