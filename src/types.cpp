#include "types.hpp"

#include <algorithm>
#include <cctype>

namespace rtx {
namespace {

std::string lower_copy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

}  // namespace

std::string to_string(Direction direction) {
  return direction == Direction::Input ? "input" : "output";
}

std::string to_string(Role role) {
  switch (role) {
    case Role::Client:
      return "client";
    case Role::Listener:
      return "listener";
    case Role::Server:
      return "server";
    case Role::Rendezvous:
      return "rendezvous";
  }
  return "client";
}

bool parse_role(const std::string& text, Role& role) {
  const std::string value = lower_copy(text);
  if (value == "client") {
    role = Role::Client;
    return true;
  }
  if (value == "listener") {
    role = Role::Listener;
    return true;
  }
  if (value == "server") {
    role = Role::Server;
    return true;
  }
  if (value == "rendezvous") {
    role = Role::Rendezvous;
    return true;
  }
  return false;
}

bool parse_direction(const std::string& text, Direction& direction) {
  const std::string value = lower_copy(text);
  if (value == "input" || value == "in") {
    direction = Direction::Input;
    return true;
  }
  if (value == "output" || value == "out") {
    direction = Direction::Output;
    return true;
  }
  return false;
}

}  // namespace rtx
