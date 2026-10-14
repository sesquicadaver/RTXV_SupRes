#include "catalog.hpp"

#include <cassert>
#include <cstdio>
#include <iostream>

int main() {
  const auto modes = rtx::discover_modes();
  assert(!modes.empty());
  const auto has = [&](rtx::Direction direction, const char* protocol, const char* role) {
    return rtx::find_mode(modes, direction, protocol, role) != nullptr;
  };
  for (const rtx::Mode& mode : modes) {
    assert(mode.protocol != "gopher");
    assert(mode.protocol != "crypto");
    assert(mode.protocol != "md5");
  }
  assert(has(rtx::Direction::Input, "udp", "listener"));
  assert(has(rtx::Direction::Output, "udp", "client"));
  assert(!has(rtx::Direction::Output, "udp", "server"));
  assert(!has(rtx::Direction::Input, "udp", "client"));
  const bool rtsp_server = has(rtx::Direction::Output, "rtsp", "server");
  const bool rtsp_listen =
      rtx::ffmpeg_has_format_const(true, "rtsp", "rtsp_flags", "listen", true);
  assert(rtsp_server == rtsp_listen);
  const bool http_server = has(rtx::Direction::Output, "http", "server");
  assert(http_server == rtx::ffmpeg_has_protocol_option("http", "listen", true));
  const bool rtmp_out_listen = has(rtx::Direction::Output, "rtmp", "listener");
  const bool rtmp_listen_option = rtx::ffmpeg_has_protocol_option("rtmp", "rtmp_listen", true) ||
                                  rtx::ffmpeg_has_protocol_option("rtmp", "listen", true);
  assert(rtmp_out_listen == rtmp_listen_option);
  if (has(rtx::Direction::Input, "rtsp", "client")) {
    const rtx::Mode* mode = rtx::find_mode(modes, rtx::Direction::Input, "rtsp", "client");
    assert(mode != nullptr);
    bool saw_min = false;
    bool saw_max = false;
    for (const std::string& field : mode->optional) {
      saw_min = saw_min || field == "rtp_port_min";
      saw_max = saw_max || field == "rtp_port_max";
    }
    assert(saw_min && saw_max);
  }
  const rtx::Mode* rtp = rtx::find_mode(modes, rtx::Direction::Output, "rtp", "client");
  if (rtp != nullptr) {
    bool saw_rtcp = false;
    for (const std::string& field : rtp->optional) {
      saw_rtcp = saw_rtcp || field == "rtcp_port";
    }
    assert(saw_rtcp);
  }
  std::cout << "modes " << modes.size() << '\n';
  return 0;
}
