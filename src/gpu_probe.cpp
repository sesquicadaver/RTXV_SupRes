#include "gpu_probe.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rtx {
namespace {

constexpr int kCcMajor = 75;
constexpr int kCcMinor = 76;

struct CudaApi {
  void* library = nullptr;
  int (*init)(unsigned) = nullptr;
  int (*device_count)(int*) = nullptr;
  int (*device_get)(int*, int) = nullptr;
  int (*device_name)(char*, int, int) = nullptr;
  int (*device_attr)(int*, int, int) = nullptr;
  int (*device_uuid)(unsigned char*, int) = nullptr;
  int (*device_memory)(std::uint64_t*, int) = nullptr;

  ~CudaApi() {
#if defined(_WIN32)
    if (library != nullptr) {
      FreeLibrary(static_cast<HMODULE>(library));
    }
#else
    if (library != nullptr) {
      dlclose(library);
    }
#endif
  }
};

void* load_symbol(void* library, const char* name) {
#if defined(_WIN32)
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
  return dlsym(library, name);
#endif
}

bool load_cuda(CudaApi& api, std::string& note) {
#if defined(_WIN32)
  api.library = LoadLibraryA("nvcuda.dll");
#else
  api.library = dlopen("libcuda.so.1", RTLD_LAZY);
#endif
  if (api.library == nullptr) {
    note = "бібліотеку CUDA-драйвера не знайдено";
    return false;
  }
  api.init = reinterpret_cast<int (*)(unsigned)>(load_symbol(api.library, "cuInit"));
  api.device_count = reinterpret_cast<int (*)(int*)>(load_symbol(api.library, "cuDeviceGetCount"));
  api.device_get = reinterpret_cast<int (*)(int*, int)>(load_symbol(api.library, "cuDeviceGet"));
  api.device_name = reinterpret_cast<int (*)(char*, int, int)>(load_symbol(api.library, "cuDeviceGetName"));
  api.device_attr = reinterpret_cast<int (*)(int*, int, int)>(load_symbol(api.library, "cuDeviceGetAttribute"));
  api.device_uuid = reinterpret_cast<int (*)(unsigned char*, int)>(load_symbol(api.library, "cuDeviceGetUuid"));
  api.device_memory =
      reinterpret_cast<int (*)(std::uint64_t*, int)>(load_symbol(api.library, "cuDeviceTotalMem_v2"));
  if (api.device_memory == nullptr) {
    api.device_memory = reinterpret_cast<int (*)(std::uint64_t*, int)>(load_symbol(api.library, "cuDeviceTotalMem"));
  }
  if (api.init == nullptr || api.device_count == nullptr || api.device_get == nullptr || api.device_name == nullptr ||
      api.device_attr == nullptr) {
    note = "CUDA-драйвер не експортує потрібні функції";
    return false;
  }
  if (api.init(0) != 0) {
    note = "cuInit не вдався";
    return false;
  }
  return true;
}

}  // namespace

std::vector<GpuInfo> probe_cuda_devices(std::string& note) {
  std::vector<GpuInfo> devices;
  CudaApi api;
  if (!load_cuda(api, note)) {
    return devices;
  }
  int count = 0;
  if (api.device_count(&count) != 0 || count <= 0) {
    note = "CUDA-пристроїв немає";
    return devices;
  }
  for (int index = 0; index < count; ++index) {
    int device = 0;
    if (api.device_get(&device, index) != 0) {
      continue;
    }
    GpuInfo info;
    info.index = index;
    char name[256] = {};
    if (api.device_name(name, static_cast<int>(sizeof(name)), device) == 0) {
      info.name = name;
    }
    api.device_attr(&info.cc_major, kCcMajor, device);
    api.device_attr(&info.cc_minor, kCcMinor, device);
    unsigned char uuid[16] = {};
    if (api.device_uuid != nullptr && api.device_uuid(uuid, device) == 0) {
      char hex[40] = {};
      for (int byte = 0; byte < 16; ++byte) {
        std::snprintf(hex + byte * 2, 3, "%02x", uuid[byte]);
      }
      info.uuid = std::string("GPU-") + hex;
    }
    if (api.device_memory != nullptr) {
      std::uint64_t bytes = 0;
      if (api.device_memory(&bytes, device) == 0) {
        info.vram_bytes = bytes;
      }
    }
    devices.push_back(info);
  }
  if (devices.empty()) {
    note = "CUDA-пристрої не прочитано";
  } else {
    note = "перелік GPU прочитано; придатність до RTX VSR підтверджує SDK під час створення ефекту";
  }
  return devices;
}

}  // namespace rtx
