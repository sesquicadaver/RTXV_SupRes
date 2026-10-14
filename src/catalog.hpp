#pragma once

#include "endpoint.hpp"

#include <string>
#include <vector>

namespace rtx {

std::vector<Mode> discover_modes();
const OptionProbe& ffmpeg_probe();
std::string modes_text(const std::vector<Mode>& modes);
std::string modes_json(const std::vector<Mode>& modes);

bool ffmpeg_has_protocol_option(const std::string& protocol, const std::string& name, bool output);
bool ffmpeg_has_format_const(bool muxer, const std::string& format, const std::string& unit,
                             const std::string& name, bool output);

}  // namespace rtx
