#pragma once

#include "types.hpp"

#include <functional>
#include <string>

namespace rtx {

class OptionProbe {
 public:
  virtual ~OptionProbe() = default;
  virtual bool protocol_option(const std::string& protocol, const std::string& name, bool output) const = 0;
  virtual bool format_option(const std::string& format, const std::string& name, bool output) const = 0;
};

std::string validate_endpoint(const Endpoint& endpoint, const Mode& mode);
std::string build_endpoint(const Endpoint& endpoint, const Mode& mode, const OptionProbe& probe,
                           BuiltEndpoint& built);

const Mode* find_mode(const std::vector<Mode>& modes, Direction direction, const std::string& protocol,
                      const std::string& role, const std::string& container = {});

std::string port_caption(Direction direction, Role role);
std::string consumer_address(const Endpoint& endpoint);

}  // namespace rtx
