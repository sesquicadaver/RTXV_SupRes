#include "endpoint.hpp"
#include "check.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {

class YesProbe final : public rtx::OptionProbe {
 public:
  bool protocol_option(const std::string&, const std::string&, bool) const override { return true; }
  bool format_option(const std::string&, const std::string&, bool) const override { return true; }
};

class NoProbe final : public rtx::OptionProbe {
 public:
  bool protocol_option(const std::string&, const std::string&, bool) const override { return false; }
  bool format_option(const std::string&, const std::string&, bool) const override { return false; }
};

rtx::Mode mode_of(std::string protocol, rtx::Direction direction, rtx::Role role, std::vector<std::string> required,
                  std::vector<std::string> optional, bool url_ok, std::string container = {}) {
  rtx::Mode mode;
  mode.protocol = std::move(protocol);
  mode.direction = direction;
  mode.role = role;
  mode.provider = "test";
  mode.ffmpeg_name = mode.protocol;
  mode.container = std::move(container);
  mode.required = std::move(required);
  mode.optional = std::move(optional);
  mode.url_satisfies_location = url_ok;
  return mode;
}

void expect_ok(const char* name, const std::string& error) {
  if (!error.empty()) {
    std::cerr << name << ": " << error << '\n';
  }
  CHECK(error.empty());
}

}  // namespace

int main() {
  const YesProbe yes;
  const NoProbe no;
  const std::string path = "rtx-vsr-proxy-endpoint-test.bin";
  {
    std::ofstream file(path);
    file << "x";
  }

  rtx::Endpoint file_in;
  file_in.protocol = "file";
  file_in.role = "client";
  file_in.path = path;
  const auto file_mode = mode_of("file", rtx::Direction::Input, rtx::Role::Client, {"path"}, {}, true);
  rtx::BuiltEndpoint built;
  expect_ok("file", rtx::build_endpoint(file_in, file_mode, yes, built));
  CHECK(built.ffmpeg_url == path);
  file_in.local_port = 1;
  CHECK(!rtx::validate_endpoint(file_in, file_mode).empty());

  rtx::Endpoint udp_in;
  udp_in.protocol = "udp";
  udp_in.role = "listener";
  udp_in.local_port = 5600;
  const auto udp_listen = mode_of("udp", rtx::Direction::Input, rtx::Role::Listener, {"local_port"},
                                  {"local_host", "params"}, true, "mpegts");
  expect_ok("udp-in", rtx::build_endpoint(udp_in, udp_listen, yes, built));
  CHECK(built.ffmpeg_url.find("udp://0.0.0.0:5600?") == 0);
  CHECK(built.ffmpeg_url.find("fifo_size=64") != std::string::npos);
  CHECK(built.ffmpeg_url.find("overrun_nonfatal=1") != std::string::npos);
  udp_in.remote_port = 9;
  CHECK(!rtx::validate_endpoint(udp_in, udp_listen).empty());

  rtx::Endpoint udp_out;
  udp_out.protocol = "udp";
  udp_out.role = "client";
  udp_out.remote_host = "192.168.1.10";
  udp_out.remote_port = 5600;
  udp_out.local_port = 47000;
  const auto udp_send = mode_of("udp", rtx::Direction::Output, rtx::Role::Client, {"remote_host", "remote_port"},
                                {"local_host", "local_port", "params"}, true, "mpegts");
  expect_ok("udp-out", rtx::build_endpoint(udp_out, udp_send, yes, built));
  CHECK(built.ffmpeg_url.find("udp://192.168.1.10:5600?") == 0);
  CHECK(built.ffmpeg_url.find("localport=47000") != std::string::npos);
  CHECK(built.ffmpeg_url.find("pkt_size=1316") != std::string::npos);

  rtx::Endpoint srt;
  srt.protocol = "srt";
  srt.role = "listener";
  srt.local_port = 9000;
  const auto srt_listen = mode_of("srt", rtx::Direction::Input, rtx::Role::Listener, {"local_port"},
                                  {"local_host", "params"}, true, "mpegts");
  expect_ok("srt", rtx::build_endpoint(srt, srt_listen, yes, built));
  CHECK(built.ffmpeg_url.find("mode=listener") != std::string::npos);
  CHECK(built.ffmpeg_url.find("latency=20000") != std::string::npos);
  srt.role = "client";
  srt.local_port = -1;
  srt.remote_host = "10.0.0.2";
  srt.remote_port = 9000;
  srt.local_port = 1;
  const auto srt_caller = mode_of("srt", rtx::Direction::Output, rtx::Role::Client, {"remote_host", "remote_port"},
                                  {"local_port", "params"}, true, "mpegts");
  srt.role = "client";
  CHECK(!rtx::build_endpoint(srt, srt_caller, yes, built).empty());

  rtx::Endpoint rtsp;
  rtsp.protocol = "rtsp";
  rtsp.role = "listener";
  rtsp.local_port = 8554;
  rtsp.path = "live";
  rtsp.rtp_port_min = 5000;
  rtsp.rtp_port_max = 5010;
  const auto rtsp_listen =
      mode_of("rtsp", rtx::Direction::Input, rtx::Role::Listener, {"local_port"},
              {"local_host", "path", "rtp_port_min", "rtp_port_max", "params"}, true);
  expect_ok("rtsp", rtx::build_endpoint(rtsp, rtsp_listen, yes, built));
  CHECK(built.ffmpeg_url == "rtsp://0.0.0.0:8554/live");
  CHECK(built.format_options.at("rtsp_flags") == "listen");
  CHECK(built.format_options.at("reorder_queue_size") == "0");
  CHECK(built.format_options.at("min_port") == "5000");
  CHECK(built.format_options.at("max_port") == "5010");

  rtx::Endpoint http;
  http.protocol = "http";
  http.role = "server";
  http.local_host = "127.0.0.1";
  http.local_port = 8080;
  http.path = "/stream";
  const auto http_server = mode_of("http", rtx::Direction::Output, rtx::Role::Server, {"local_port"},
                                   {"local_host", "path", "params"}, true, "mpegts");
  expect_ok("http", rtx::build_endpoint(http, http_server, yes, built));
  CHECK(built.ffmpeg_url == "http://127.0.0.1:8080/stream");
  CHECK(built.protocol_options.at("listen") == "1");
  http.params.emplace_back("evil", "1");
  CHECK(!rtx::build_endpoint(http, http_server, no, built).empty());

  rtx::Endpoint rtmp;
  rtmp.protocol = "rtmp";
  rtmp.role = "client";
  rtmp.remote_host = "live.example";
  rtmp.app = "app";
  rtmp.playpath = "stream";
  const auto rtmp_pub = mode_of("rtmp", rtx::Direction::Output, rtx::Role::Client, {"remote_host", "app", "playpath"},
                                {"remote_port", "params"}, true, "flv");
  expect_ok("rtmp", rtx::build_endpoint(rtmp, rtmp_pub, yes, built));
  CHECK(built.ffmpeg_url == "rtmp://live.example:1935/app/stream");

  std::remove(path.c_str());
  return rtx_test::result();
}
