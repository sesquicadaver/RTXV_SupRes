#pragma once

#include <csignal>
#include <cstdint>
#include <cstddef>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rtx {

inline void socket_startup() {
#if defined(_WIN32)
  static bool ready = false;
  if (!ready) {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    ready = true;
  }
#else
  (void)0;
#endif
}

inline int process_id() {
#if defined(_WIN32)
  return static_cast<int>(::_getpid());
#else
  return static_cast<int>(::getpid());
#endif
}

inline void ignore_pipe_signal() {
#if !defined(_WIN32)
  std::signal(SIGPIPE, SIG_IGN);
#endif
}

inline std::intptr_t socket_open(int family, int type, int protocol) {
#if defined(_WIN32)
  const SOCKET fd = ::socket(family, type, protocol);
  if (fd == INVALID_SOCKET) {
    return -1;
  }
  return static_cast<std::intptr_t>(fd);
#else
  return static_cast<std::intptr_t>(::socket(family, type, protocol));
#endif
}

inline bool socket_ok(std::intptr_t fd) { return fd >= 0; }

inline int socket_close(std::intptr_t fd) {
#if defined(_WIN32)
  return ::closesocket(static_cast<SOCKET>(fd));
#else
  return ::close(static_cast<int>(fd));
#endif
}

inline int socket_shutdown(std::intptr_t fd) {
#if defined(_WIN32)
  return ::shutdown(static_cast<SOCKET>(fd), SD_BOTH);
#else
  return ::shutdown(static_cast<int>(fd), SHUT_RDWR);
#endif
}

inline int socket_reuse(std::intptr_t fd) {
  const int reuse = 1;
#if defined(_WIN32)
  return ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                      sizeof(reuse));
#else
  return ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
}

inline void socket_timeouts(std::intptr_t fd, int recv_ms, int send_ms) {
#if defined(_WIN32)
  const DWORD recv_timeout = static_cast<DWORD>(recv_ms);
  const DWORD send_timeout = static_cast<DWORD>(send_ms);
  ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recv_timeout),
               sizeof(recv_timeout));
  ::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_timeout),
               sizeof(send_timeout));
#else
  timeval recv_timeout {};
  recv_timeout.tv_sec = recv_ms / 1000;
  recv_timeout.tv_usec = (recv_ms % 1000) * 1000;
  timeval send_timeout {};
  send_timeout.tv_sec = send_ms / 1000;
  send_timeout.tv_usec = (send_ms % 1000) * 1000;
  ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
  ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
#endif
}

inline int socket_bind(std::intptr_t fd, const addrinfo* address) {
#if defined(_WIN32)
  return ::bind(static_cast<SOCKET>(fd), address->ai_addr, static_cast<int>(address->ai_addrlen));
#else
  return ::bind(static_cast<int>(fd), address->ai_addr, address->ai_addrlen);
#endif
}

inline int socket_listen(std::intptr_t fd, int backlog) {
#if defined(_WIN32)
  return ::listen(static_cast<SOCKET>(fd), backlog);
#else
  return ::listen(static_cast<int>(fd), backlog);
#endif
}

inline std::intptr_t socket_accept(std::intptr_t fd) {
#if defined(_WIN32)
  const SOCKET client = ::accept(static_cast<SOCKET>(fd), nullptr, nullptr);
  if (client == INVALID_SOCKET) {
    return -1;
  }
  return static_cast<std::intptr_t>(client);
#else
  return static_cast<std::intptr_t>(::accept(static_cast<int>(fd), nullptr, nullptr));
#endif
}

inline long socket_send(std::intptr_t fd, const char* data, std::size_t size) {
  const int chunk = static_cast<int>(size > 1048576 ? 1048576 : size);
#if defined(_WIN32)
  return ::send(static_cast<SOCKET>(fd), data, chunk, 0);
#else
  return ::send(static_cast<int>(fd), data, static_cast<std::size_t>(chunk), MSG_NOSIGNAL);
#endif
}

inline long socket_recvfrom(std::intptr_t fd, char* data, std::size_t size) {
  const int chunk = static_cast<int>(size > 65535 ? 65535 : size);
#if defined(_WIN32)
  return ::recvfrom(static_cast<SOCKET>(fd), data, chunk, 0, nullptr, nullptr);
#else
  return ::recvfrom(static_cast<int>(fd), data, static_cast<std::size_t>(chunk), 0, nullptr, nullptr);
#endif
}

// 1 — рядок прочитано, 0 — таймаут, -1 — кінець stdin.
inline int poll_stdin_line(std::string& line, int timeout_ms) {
  static std::string pending;
#if defined(_WIN32)
  const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
  DWORD available = 0;
  if (handle == INVALID_HANDLE_VALUE || !PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) ||
      available == 0) {
    Sleep(static_cast<DWORD>(timeout_ms));
    return 0;
  }
  char buffer[256];
  DWORD read = 0;
  if (!ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) || read == 0) {
    return -1;
  }
  pending.append(buffer, buffer + read);
#else
  pollfd descriptor{};
  descriptor.fd = STDIN_FILENO;
  descriptor.events = POLLIN;
  const int ready = ::poll(&descriptor, 1, timeout_ms);
  if (ready == 0) {
    return 0;
  }
  if (ready < 0) {
    return errno == EINTR ? 0 : -1;
  }
  char buffer[256];
  const ssize_t read = ::read(STDIN_FILENO, buffer, sizeof(buffer));
  if (read < 0) {
    return errno == EINTR ? 0 : -1;
  }
  if (read == 0) {
    return -1;
  }
  pending.append(buffer, buffer + read);
#endif
  const auto split = pending.find('\n');
  if (split == std::string::npos) {
    return 0;
  }
  line = pending.substr(0, split);
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  pending.erase(0, split + 1);
  return 1;
}

inline long socket_recv(std::intptr_t fd, char* data, std::size_t size) {
  const int chunk = static_cast<int>(size > 1048576 ? 1048576 : size);
#if defined(_WIN32)
  return ::recv(static_cast<SOCKET>(fd), data, chunk, 0);
#else
  return ::recv(static_cast<int>(fd), data, static_cast<std::size_t>(chunk), 0);
#endif
}

}  // namespace rtx
