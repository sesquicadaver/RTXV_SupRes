#pragma once

#include "types.hpp"

#include <string>
#include <vector>

namespace rtx {

std::vector<GpuInfo> probe_cuda_devices(std::string& note);

// true, якщо завантажувач бачить драйверну бібліотеку (nvcuda.dll / libcuda.so.1).
bool cuda_driver_library_present();

}  // namespace rtx
