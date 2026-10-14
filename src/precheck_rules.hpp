#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtx {

/// Номер драйвера NVIDIA з файлової версії nvlddmkm.sys: 32.0.15.7216 → 57216 (572.16).
int nvidia_driver_number(unsigned branch, unsigned build);

/// PCI DEV_ з рядка Hardware ID. Порожньо, якщо це не адаптер NVIDIA.
bool nvidia_device_id(const std::string& hardware_id, std::uint32_t& device_id);

enum class RuntimeChoice { Keep, Install };

enum class DriverChoice { Keep, InstallOfficial, Ambiguous, Absent, Unspecified };

struct ReleaseManifest {
  int windows_build_min = 22000;
  std::uint64_t disk_bytes = 0;
  int vc_redist_build = 0;
  std::string vc_file;
  std::string vc_sha256;
  int min_driver = 0;
  std::vector<std::uint32_t> device_ids;
  std::string driver_file;
  std::string driver_url;
  std::string driver_sha256;
};

struct HostFacts {
  int windows_build = 0;
  bool x64 = false;
  std::uint64_t free_bytes = 0;
  bool app_running = false;
  bool runtime_installed = false;
  int runtime_build = 0;
  std::vector<std::uint32_t> nvidia_device_ids;
  bool driver_present = false;
  int driver_version = 0;
};

struct PrecheckResult {
  bool proceed = false;
  RuntimeChoice runtime = RuntimeChoice::Keep;
  DriverChoice driver = DriverChoice::Unspecified;
  std::string vc_file;
  std::string vc_sha256;
  std::string driver_file;
  std::string driver_url;
  std::string driver_sha256;
  std::string message;
};

/// Маніфест релізу. Порожні суми, драйвер і перелік GPU лишаються порожніми: їх заповнює пакування.
bool parse_release_manifest(const std::string& text, ReleaseManifest& manifest, std::string& error);

/// Рішення до запуску CUDA-програми. Новіший сумісний runtime лишається. Невідомий OEM не отримує «близький» драйвер.
PrecheckResult evaluate_precheck(const HostFacts& host, const ReleaseManifest& manifest);

std::string precheck_json(const PrecheckResult& result);

}  // namespace rtx
