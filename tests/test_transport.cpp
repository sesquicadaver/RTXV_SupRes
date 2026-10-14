#include "endpoint.hpp"
#include "platform.hpp"
#include "check.hpp"

#include <atomic>
#include <chrono>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

namespace {

class YesProbe final : public rtx::OptionProbe {
 public:
  bool protocol_option(const std::string&, const std::string&, bool) const override { return true; }
  bool format_option(const std::string&, const std::string&, bool) const override { return true; }
};

int interrupt_cb(void* opaque) {
  return static_cast<std::atomic<bool>*>(opaque)->load() ? 0 : 1;
}

int bind_socket(std::intptr_t fd, sockaddr_in& address) {
#if defined(_WIN32)
  return ::bind(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&address), static_cast<int>(sizeof(address)));
#else
  return ::bind(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&address), sizeof(address));
#endif
}

int socket_name(std::intptr_t fd, sockaddr_in& address) {
#if defined(_WIN32)
  int length = sizeof(address);
  return ::getsockname(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&address), &length);
#else
  socklen_t length = sizeof(address);
  return ::getsockname(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&address), &length);
#endif
}

int connect_socket(std::intptr_t fd, const sockaddr_in& address) {
#if defined(_WIN32)
  return ::connect(static_cast<SOCKET>(fd), reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address)));
#else
  return ::connect(static_cast<int>(fd), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
#endif
}

int free_port() {
  rtx::socket_startup();
  const std::intptr_t fd = rtx::socket_open(AF_INET, SOCK_STREAM, 0);
  CHECK(rtx::socket_ok(fd));
  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  CHECK(bind_socket(fd, address) == 0);
  CHECK(socket_name(fd, address) == 0);
  const int port = ntohs(address.sin_port);
  rtx::socket_close(fd);
  return port;
}

bool connect_http(int port) {
  const std::intptr_t fd = rtx::socket_open(AF_INET, SOCK_STREAM, 0);
  if (!rtx::socket_ok(fd)) {
    return false;
  }
  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const bool ok = connect_socket(fd, address) == 0;
  if (ok) {
    const char request[] = "GET /stream HTTP/1.0\r\n\r\n";
    rtx::socket_send(fd, request, sizeof(request) - 1);
  }
  rtx::socket_close(fd);
  return ok;
}

bool http_accepts(const std::string& url, bool dictionary, int port) {
  std::atomic<bool> run{true};
  std::thread thread([&] {
    AVDictionary* options = nullptr;
    if (dictionary) {
      av_dict_set(&options, "listen", "1", 0);
    }
    AVIOInterruptCB callback {interrupt_cb, &run};
    AVIOContext* io = nullptr;
    const int status = avio_open2(&io, url.c_str(), AVIO_FLAG_WRITE, &callback, &options);
    av_dict_free(&options);
    if (status >= 0 && io != nullptr) {
      avio_closep(&io);
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  const bool accepted = connect_http(port);
  run.store(false);
  thread.join();
  return accepted;
}

}  // namespace

int main() {
  avformat_network_init();
  const YesProbe probe;
  rtx::Endpoint http;
  http.protocol = "http";
  http.role = "server";
  http.local_host = "127.0.0.1";
  http.local_port = 8080;
  http.path = "/stream";
  rtx::Mode mode;
  mode.protocol = "http";
  mode.direction = rtx::Direction::Output;
  mode.role = rtx::Role::Server;
  mode.ffmpeg_name = "http";
  mode.container = "mpegts";
  mode.required = {"local_port"};
  mode.optional = {"local_host", "path", "params"};
  mode.url_satisfies_location = true;
  rtx::BuiltEndpoint built;
  CHECK(rtx::build_endpoint(http, mode, probe, built).empty());
  CHECK(built.ffmpeg_url == "http://127.0.0.1:8080/stream");
  CHECK(built.protocol_options.at("listen") == "1");

  rtx::Endpoint rtmp;
  rtmp.protocol = "rtmp";
  rtmp.role = "listener";
  rtmp.local_port = 1935;
  rtmp.app = "live";
  rtmp.playpath = "cam";
  rtx::Mode rtmp_mode;
  rtmp_mode.protocol = "rtmp";
  rtmp_mode.direction = rtx::Direction::Input;
  rtmp_mode.role = rtx::Role::Listener;
  rtmp_mode.ffmpeg_name = "rtmp";
  rtmp_mode.required = {"local_port", "app", "playpath"};
  rtmp_mode.optional = {"local_host", "params"};
  rtmp_mode.url_satisfies_location = true;
  CHECK(rtx::build_endpoint(rtmp, rtmp_mode, probe, built).empty());
  CHECK(built.ffmpeg_url == "rtmp://0.0.0.0:1935/live/cam");
  CHECK(built.ffmpeg_url.find("listen") == std::string::npos);
  CHECK(built.protocol_options.at("rtmp_listen") == "1");
  CHECK(built.protocol_options.at("rtmp_live") == "-1");

  const int query_port = free_port();
  const int dict_port = free_port();
  CHECK(!http_accepts("http://127.0.0.1:" + std::to_string(query_port) + "/stream?listen=1", false, query_port));
  CHECK(http_accepts("http://127.0.0.1:" + std::to_string(dict_port) + "/stream", true, dict_port));
  return rtx_test::result();
}
