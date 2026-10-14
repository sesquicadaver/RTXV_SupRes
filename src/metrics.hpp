#pragma once

#include "types.hpp"

#include <string>

namespace rtx {

std::string metrics_json(const Metrics& metrics);
bool write_metrics_file(const std::string& path, const Metrics& metrics);

}  // namespace rtx
