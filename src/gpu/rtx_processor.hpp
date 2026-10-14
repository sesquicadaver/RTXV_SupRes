#pragma once

#include "processor.hpp"

#include <memory>
#include <string>

namespace rtx {

std::unique_ptr<FrameProcessor> create_rtx_processor();
std::string probe_rtx_sdk(int gpu_index);

}  // namespace rtx
