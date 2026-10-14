#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace rtx {

class HlsServer {
 public:
  HlsServer() = default;
  ~HlsServer();

  HlsServer(const HlsServer&) = delete;
  HlsServer& operator=(const HlsServer&) = delete;

  bool start(const std::string& host, int port, const std::string& root, std::string& error);
  void stop();

 private:
  void loop();

  std::atomic<bool> running_{false};
  std::intptr_t listen_fd_ = -1;
  std::string root_;
  std::thread thread_;
};

}  // namespace rtx
