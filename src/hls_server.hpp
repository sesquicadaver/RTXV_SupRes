#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
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
  void serve_client(std::intptr_t client);
  void wait_inflight();

  std::atomic<bool> running_{false};
  std::atomic<int> inflight_{0};
  std::intptr_t listen_fd_ = -1;
  std::filesystem::path root_;
  std::thread thread_;
};

}  // namespace rtx
