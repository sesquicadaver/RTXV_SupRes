#include "catalog.hpp"
#include "check.hpp"

#include <iostream>
#include <string>

int main() {
  const auto modes = rtx::discover_modes();
  CHECK(!modes.empty());
  const auto has = [&](rtx::Direction direction, const char* protocol, const char* role) {
    return rtx::find_mode(modes, direction, protocol, role) != nullptr;
  };
  for (const rtx::Mode& mode : modes) {
    CHECK(mode.protocol != "gopher");
    CHECK(mode.protocol != "crypto");
    CHECK(mode.protocol != "md5");
  }
  CHECK(has(rtx::Direction::Input, "udp", "listener"));
  CHECK(has(rtx::Direction::Output, "udp", "client"));
  CHECK(!has(rtx::Direction::Output, "udp", "server"));
  CHECK(!has(rtx::Direction::Input, "udp", "client"));
  const bool rtsp_server = has(rtx::Direction::Output, "rtsp", "server");
  const bool rtsp_listen =
      rtx::ffmpeg_has_format_const(true, "rtsp", "rtsp_flags", "listen", true);
  CHECK(rtsp_server == rtsp_listen);
  const bool http_server = has(rtx::Direction::Output, "http", "server");
  CHECK(http_server == rtx::ffmpeg_has_protocol_option("http", "listen", true));
  const bool rtmp_out_listen = has(rtx::Direction::Output, "rtmp", "listener");
  const bool rtmp_listen_option = rtx::ffmpeg_has_protocol_option("rtmp", "rtmp_listen", true) ||
                                  rtx::ffmpeg_has_protocol_option("rtmp", "listen", true);
  CHECK(rtmp_out_listen == rtmp_listen_option);
  if (has(rtx::Direction::Input, "rtsp", "client")) {
    const rtx::Mode* mode = rtx::find_mode(modes, rtx::Direction::Input, "rtsp", "client");
    CHECK(mode != nullptr);
    if (mode == nullptr) {
      return rtx_test::result();
    }
    bool saw_min = false;
    bool saw_max = false;
    for (const std::string& field : mode->optional) {
      saw_min = saw_min || field == "rtp_port_min";
      saw_max = saw_max || field == "rtp_port_max";
    }
    CHECK(saw_min && saw_max);
  }
  const rtx::Mode* rtp_ts = rtx::find_mode(modes, rtx::Direction::Output, "rtp", "client", "rtp_mpegts");
  const rtx::Mode* rtp_default = rtx::find_mode(modes, rtx::Direction::Output, "rtp", "client");
  const rtx::Mode* rtp_es = rtx::find_mode(modes, rtx::Direction::Output, "rtp", "client", "rtp");
  const rtx::Mode* rtp_sdp = rtx::find_mode(modes, rtx::Direction::Input, "rtp", "client", "sdp");
  const rtx::Mode* rtp_in = rtx::find_mode(modes, rtx::Direction::Input, "rtp", "listener", "rtp");
  CHECK(rtp_ts != nullptr);
  CHECK(rtp_default == rtp_ts);
  CHECK(rtx::find_mode(modes, rtx::Direction::Output, "rtp", "client", "mpegts") == nullptr);
  CHECK(rtp_es != nullptr);
  CHECK(rtp_sdp != nullptr);
  CHECK(rtp_in != nullptr);
  if (rtp_in != nullptr) {
    CHECK(rtp_in->container == "rtp");
    CHECK(rtp_in->ffmpeg_name == "rtp");
  }
  if (rtp_ts != nullptr) {
    CHECK(rtp_ts->container == "rtp_mpegts");
    CHECK(rtp_ts->ffmpeg_name == "rtp_mpegts");
    CHECK(rtp_ts->summary.find("MPEG-TS") != std::string::npos);
    bool saw_rtcp = false;
    for (const std::string& field : rtp_ts->optional) {
      saw_rtcp = saw_rtcp || field == "rtcp_port";
    }
    CHECK(saw_rtcp);
  }
  if (rtp_es != nullptr) {
    bool saw_sdp = false;
    for (const std::string& field : rtp_es->required) {
      saw_sdp = saw_sdp || field == "sdp";
    }
    CHECK(saw_sdp);
  }
  std::cout << "modes " << modes.size() << '\n';
  return rtx_test::result();
}
