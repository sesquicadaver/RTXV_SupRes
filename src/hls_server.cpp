#include "hls_server.hpp"

#include "platform.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace rtx {
namespace {

std::string content_type(const std::filesystem::path& path) {
  if (path.extension() == ".m3u8") {
    return "application/vnd.apple.mpegurl";
  }
  if (path.extension() == ".ts") {
    return "video/mp2t";
  }
  return "application/octet-stream";
}

bool send_all(std::intptr_t fd, const char* data, std::size_t size) {
  while (size > 0) {
    const long sent = socket_send(fd, data, size);
    if (sent <= 0) {
      return false;
    }
    data += sent;
    size -= static_cast<std::size_t>(sent);
  }
  return true;
}

void respond(std::intptr_t fd, int status, const std::string& type, const std::string& body) {
  std::ostringstream header;
  header << "HTTP/1.0 " << status << (status == 200 ? " OK" : " Not Found") << "\r\n";
  header << "Content-Type: " << type << "\r\n";
  header << "Content-Length: " << body.size() << "\r\n";
  header << "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
  const std::string head = header.str();
  if (!send_all(fd, head.data(), head.size())) {
    return;
  }
  send_all(fd, body.data(), body.size());
}

std::string request_path(std::intptr_t fd) {
  std::string data;
  char buffer[1024];
  while (data.find("\r\n") == std::string::npos && data.size() < 4096) {
    const long got = socket_recv(fd, buffer, sizeof(buffer));
    if (got <= 0) {
      return {};
    }
    data.append(buffer, buffer + got);
  }
  const auto line_end = data.find("\r\n");
  if (line_end == std::string::npos) {
    return {};
  }
  const std::string line = data.substr(0, line_end);
  if (line.compare(0, 4, "GET ") != 0) {
    return {};
  }
  const auto path_end = line.find(' ', 4);
  if (path_end == std::string::npos) {
    return {};
  }
  std::string path = line.substr(4, path_end - 4);
  const auto query = path.find('?');
  if (query != std::string::npos) {
    path.resize(query);
  }
  return path;
}

}  // namespace

HlsServer::~HlsServer() { stop(); }

bool HlsServer::start(const std::string& host, int port, const std::string& root, std::string& error) {
  socket_startup();
  std::error_code fs_error;
  const auto canonical_root = std::filesystem::weakly_canonical(root, fs_error);
  if (fs_error || !std::filesystem::is_directory(canonical_root)) {
    error = "каталог HLS недоступний";
    return false;
  }
  root_ = canonical_root.string();
  addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  addrinfo* result = nullptr;
  const std::string port_text = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
    error = "не вдалося розв'язати адресу HLS-сервера";
    return false;
  }
  std::intptr_t fd = -1;
  for (addrinfo* item = result; item != nullptr; item = item->ai_next) {
    fd = socket_open(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (!socket_ok(fd)) {
      continue;
    }
    socket_reuse(fd);
    if (socket_bind(fd, item) == 0 && socket_listen(fd, 8) == 0) {
      break;
    }
    socket_close(fd);
    fd = -1;
  }
  freeaddrinfo(result);
  if (!socket_ok(fd)) {
    error = "не вдалося відкрити порт HLS-сервера";
    return false;
  }
  listen_fd_ = fd;
  running_.store(true);
  thread_ = std::thread([this] { loop(); });
  return true;
}

void HlsServer::stop() {
  running_.store(false);
  if (socket_ok(listen_fd_)) {
    socket_shutdown(listen_fd_);
    socket_close(listen_fd_);
    listen_fd_ = -1;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void HlsServer::loop() {
  while (running_.load()) {
    const std::intptr_t client = socket_accept(listen_fd_);
    if (!socket_ok(client)) {
      if (!running_.load()) {
        break;
      }
      continue;
    }
    socket_timeouts(client, 2000, 2000);
    const std::string raw = request_path(client);
    if (raw.empty() || raw.find("..") != std::string::npos || raw.front() != '/') {
      respond(client, 404, "text/plain", "not found");
      socket_close(client);
      continue;
    }
    const std::filesystem::path file = std::filesystem::path(root_) / raw.substr(1);
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(file, error);
    const auto relative = canonical.lexically_relative(std::filesystem::path(root_));
    const bool inside = !error && !relative.empty() && *relative.begin() != "..";
    if (error || !inside || !std::filesystem::is_regular_file(canonical, error)) {
      respond(client, 404, "text/plain", "not found");
      socket_close(client);
      continue;
    }
    std::ifstream input(canonical, std::ios::binary);
    std::ostringstream body;
    body << input.rdbuf();
    respond(client, 200, content_type(canonical), body.str());
    socket_close(client);
  }
}

}  // namespace rtx
