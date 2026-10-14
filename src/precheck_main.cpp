#include "precheck_rules.hpp"

#ifndef _WIN32
#error "rtx-vsr-precheck збирається лише для Windows і не лінкує CUDA"
#else

#include <windows.h>
#include <devguid.h>
#include <setupapi.h>
#include <tlhelp32.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace rtx {
namespace {

std::string narrow(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  std::string out(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
  return out;
}

bool read_file(const std::string& path, std::string& text) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  text = buffer.str();
  return true;
}

void collect_windows(HostFacts& host) {
  using RtlGetVersionPtr = LONG(WINAPI*)(OSVERSIONINFOW*);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto rtl_get_version =
      reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"));
  OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  if (rtl_get_version != nullptr && rtl_get_version(&info) == 0) {
    host.windows_build = static_cast<int>(info.dwBuildNumber);
  }
  SYSTEM_INFO system;
  GetNativeSystemInfo(&system);
  host.x64 = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
}

void collect_disk(HostFacts& host) {
  wchar_t windows_dir[MAX_PATH] = {};
  if (GetWindowsDirectoryW(windows_dir, MAX_PATH) == 0) {
    return;
  }
  ULARGE_INTEGER free{};
  if (GetDiskFreeSpaceExW(windows_dir, &free, nullptr, nullptr)) {
    host.free_bytes = free.QuadPart;
  }
}

void collect_runtime(HostFacts& host) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64", 0,
                    KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
    return;
  }
  DWORD installed = 0;
  DWORD build = 0;
  DWORD size = sizeof(DWORD);
  RegQueryValueExW(key, L"Installed", nullptr, nullptr, reinterpret_cast<LPBYTE>(&installed), &size);
  size = sizeof(DWORD);
  RegQueryValueExW(key, L"Bld", nullptr, nullptr, reinterpret_cast<LPBYTE>(&build), &size);
  RegCloseKey(key);
  host.runtime_installed = installed == 1;
  host.runtime_build = static_cast<int>(build);
}

void read_registry_text(HKEY key, const wchar_t* name, std::wstring& value) {
  DWORD size = 0;
  if (RegQueryValueExW(key, name, nullptr, nullptr, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t)) {
    return;
  }
  std::wstring buffer(size / sizeof(wchar_t), L'\0');
  if (RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS) {
    return;
  }
  if (!buffer.empty() && buffer.back() == L'\0') {
    buffer.pop_back();
  }
  value = buffer;
}

void read_adapter_driver(HDEVINFO devices, SP_DEVINFO_DATA& info, AdapterDriver& adapter) {
  wchar_t key_name[256] = {};
  DWORD type = 0;
  DWORD needed = 0;
  if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_DRIVER, &type, reinterpret_cast<PBYTE>(key_name),
                                         sizeof(key_name), &needed) ||
      key_name[0] == L'\0') {
    return;
  }
  const std::wstring path = L"SYSTEM\\CurrentControlSet\\Control\\Class\\" + std::wstring(key_name);
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
    return;
  }
  std::wstring provider;
  std::wstring version;
  read_registry_text(key, L"ProviderName", provider);
  read_registry_text(key, L"DriverVersion", version);
  RegCloseKey(key);
  adapter.nvidia_provider = nvidia_provider_name(narrow(provider));
  adapter.version = nvidia_driver_from_version_string(narrow(version));
}

void collect_devices(HostFacts& host) {
  HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
  if (devices == INVALID_HANDLE_VALUE) {
    return;
  }
  std::vector<AdapterDriver> adapters;
  SP_DEVINFO_DATA info{};
  info.cbSize = sizeof(info);
  for (DWORD index = 0; SetupDiEnumDeviceInfo(devices, index, &info); ++index) {
    wchar_t buffer[1024] = {};
    DWORD type = 0;
    DWORD needed = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_HARDWAREID, &type, reinterpret_cast<PBYTE>(buffer),
                                           sizeof(buffer), &needed)) {
      continue;
    }
    const wchar_t* cursor = buffer;
    while (*cursor != L'\0') {
      const std::wstring item(cursor);
      std::uint32_t device_id = 0;
      if (nvidia_device_id(narrow(item), device_id)) {
        AdapterDriver adapter;
        adapter.device_id = device_id;
        read_adapter_driver(devices, info, adapter);
        adapters.push_back(adapter);
        host.nvidia_device_ids.push_back(device_id);
        break;
      }
      cursor += item.size() + 1;
    }
  }
  SetupDiDestroyDeviceInfoList(devices);
  const DriverPresence presence = summarize_display_drivers(adapters);
  host.driver_present = presence.present;
  host.driver_version = presence.version;
}

void collect_processes(HostFacts& host) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    host.app_running = true;
    return;
  }
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      std::string name = narrow(entry.szExeFile);
      for (char& symbol : name) {
        symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)));
      }
      if (name == "rtx-vsr-proxy.exe" || name == "rtx-vsr-gui.exe") {
        host.app_running = true;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
}

HostFacts collect_host() {
  HostFacts host;
  collect_windows(host);
  collect_disk(host);
  collect_runtime(host);
  collect_devices(host);
  collect_processes(host);
  return host;
}

}  // namespace

int precheck_entry(int argc, char** argv) {
  std::string manifest_path;
  std::string out_path;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if ((arg == "--manifest" || arg == "--out") && index + 1 >= argc) {
      return 2;
    }
    if (arg == "--manifest") {
      manifest_path = argv[++index];
    } else if (arg == "--out") {
      out_path = argv[++index];
    } else {
      return 2;
    }
  }
  PrecheckResult result;
  if (manifest_path.empty()) {
    result.message = "потрібен --manifest components.json";
  } else {
    std::string text;
    ReleaseManifest manifest;
    std::string error;
    if (!read_file(manifest_path, text) || !parse_release_manifest(text, manifest, error)) {
      result.message = error.empty() ? "маніфест не читається" : error;
    } else {
      result = evaluate_precheck(collect_host(), manifest);
    }
  }
  const std::string json = precheck_json(result);
  if (out_path.empty()) {
    const std::string line = json + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  } else {
    std::ofstream output(out_path, std::ios::binary);
    if (!output) {
      return 2;
    }
    output << json << '\n';
  }
  return result.proceed ? 0 : 2;
}

}  // namespace rtx

int main(int argc, char** argv) { return rtx::precheck_entry(argc, argv); }

#endif
