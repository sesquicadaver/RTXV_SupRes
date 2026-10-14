#pragma once

#include "types.hpp"

#include <string>
#include <vector>

namespace rtx {

std::vector<GpuInfo> probe_cuda_devices(std::string& note);

}  // namespace rtx
