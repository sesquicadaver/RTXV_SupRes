#include "precheck_rules.hpp"

#include "json_mini.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace rtx {
namespace {

bool listed(const std::vector<std::uint32_t>& ids, std::uint32_t id) {
  for (const std::uint32_t item : ids) {
    if (item == id) {
      return true;
    }
  }
  return false;
}

int hex_value(char symbol) {
  if (symbol >= '0' && symbol <= '9') {
    return symbol - '0';
  }
  if (symbol >= 'a' && symbol <= 'f') {
    return symbol - 'a' + 10;
  }
  if (symbol >= 'A' && symbol <= 'F') {
    return symbol - 'A' + 10;
  }
  return -1;
}

const char* runtime_name(RuntimeChoice choice) {
  return choice == RuntimeChoice::Install ? "install" : "keep";
}

std::string ascii_escape(const std::string& text) {
  std::string out;
  for (std::size_t index = 0; index < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead == '"' || lead == '\\') {
      out.push_back('\\');
      out.push_back(static_cast<char>(lead));
      ++index;
      continue;
    }
    if (lead == '\n') {
      out += "\\n";
      ++index;
      continue;
    }
    if (lead < 0x80) {
      out.push_back(static_cast<char>(lead));
      ++index;
      continue;
    }
    int width = 1;
    char32_t code = lead;
    if ((lead & 0xE0) == 0xC0 && index + 1 < text.size()) {
      width = 2;
      code = (lead & 0x1F) << 6;
      code |= static_cast<unsigned char>(text[index + 1]) & 0x3F;
    } else if ((lead & 0xF0) == 0xE0 && index + 2 < text.size()) {
      width = 3;
      code = (lead & 0x0F) << 12;
      code |= (static_cast<unsigned char>(text[index + 1]) & 0x3F) << 6;
      code |= static_cast<unsigned char>(text[index + 2]) & 0x3F;
    }
    char hex[8];
    std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned>(code > 0xFFFF ? 0xFFFD : code));
    out += hex;
    index += static_cast<std::size_t>(width);
  }
  return out;
}

const char* driver_name(DriverChoice choice) {
  switch (choice) {
    case DriverChoice::Keep:
      return "keep";
    case DriverChoice::InstallOfficial:
      return "install";
    case DriverChoice::Ambiguous:
      return "ambiguous";
    case DriverChoice::Absent:
      return "absent";
    case DriverChoice::Unspecified:
      return "unspecified";
  }
  return "unspecified";
}

}  // namespace

int nvidia_driver_number(unsigned branch, unsigned build) {
  if (branch < 10 || branch > 80 || build > 9999) {
    return 0;
  }
  return static_cast<int>((branch - 10) * 10000 + build);
}

int nvidia_driver_from_version_string(const std::string& text) {
  unsigned parts[4] = {};
  std::size_t index = 0;
  for (int part = 0; part < 4; ++part) {
    if (index >= text.size() || text[index] < '0' || text[index] > '9') {
      return 0;
    }
    unsigned value = 0;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
      const unsigned digit = static_cast<unsigned>(text[index] - '0');
      if (value > (0xffffffffu - digit) / 10) {
        return 0;
      }
      value = value * 10 + digit;
      ++index;
    }
    parts[part] = value;
    if (part < 3) {
      if (index >= text.size() || text[index] != '.') {
        return 0;
      }
      ++index;
    }
  }
  if (index != text.size()) {
    return 0;
  }
  return nvidia_driver_number(parts[2], parts[3]);
}

bool nvidia_provider_name(const std::string& name) {
  std::size_t begin = 0;
  while (begin < name.size() && std::isspace(static_cast<unsigned char>(name[begin])) != 0) {
    ++begin;
  }
  std::size_t end = name.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(name[end - 1])) != 0) {
    --end;
  }
  std::string lower;
  lower.reserve(end - begin);
  for (std::size_t index = begin; index < end; ++index) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(name[index]))));
  }
  return lower == "nvidia" || lower.rfind("nvidia ", 0) == 0;
}

DriverPresence summarize_display_drivers(const std::vector<AdapterDriver>& adapters) {
  DriverPresence result;
  if (adapters.empty()) {
    return result;
  }
  int lowest = 0;
  for (const AdapterDriver& adapter : adapters) {
    if (!adapter.nvidia_provider || adapter.version <= 0) {
      return DriverPresence{};
    }
    if (lowest == 0 || adapter.version < lowest) {
      lowest = adapter.version;
    }
  }
  result.present = true;
  result.version = lowest;
  return result;
}

bool nvidia_device_id(const std::string& hardware_id, std::uint32_t& device_id) {
  const auto vendor = hardware_id.find("VEN_");
  const auto device = hardware_id.find("DEV_");
  if (vendor == std::string::npos || device == std::string::npos || hardware_id.size() < device + 8) {
    return false;
  }
  int vendor_value = 0;
  for (int index = 0; index < 4; ++index) {
    const int digit = hex_value(hardware_id[vendor + 4 + static_cast<std::size_t>(index)]);
    if (digit < 0) {
      return false;
    }
    vendor_value = (vendor_value << 4) | digit;
  }
  if (vendor_value != 0x10DE) {
    return false;
  }
  int value = 0;
  for (int index = 0; index < 4; ++index) {
    const int digit = hex_value(hardware_id[device + 4 + static_cast<std::size_t>(index)]);
    if (digit < 0) {
      return false;
    }
    value = (value << 4) | digit;
  }
  device_id = static_cast<std::uint32_t>(value);
  return true;
}

bool parse_release_manifest(const std::string& text, ReleaseManifest& manifest, std::string& error) {
  const std::string body = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                                   static_cast<unsigned char>(text[1]) == 0xBB &&
                                   static_cast<unsigned char>(text[2]) == 0xBF
                               ? text.substr(3)
                               : text;
  Json root;
  if (!parse_json(body, root, error) || root.kind != Json::Kind::Object) {
    if (error.empty()) {
      error = "маніфест має бути об'єктом JSON";
    }
    return false;
  }
  manifest = ReleaseManifest{};
  manifest.windows_build_min = root.int_or("windows_build_min", 22000);
  manifest.disk_bytes = static_cast<std::uint64_t>(root.i64_or("disk_bytes", 0));
  manifest.vc_redist_build = root.int_or("vc_redist_build", 0);
  manifest.vc_file = root.text_or("vc_file", {});
  manifest.vc_sha256 = root.text_or("vc_sha256", {});
  manifest.min_driver = root.int_or("min_driver", 0);
  manifest.driver_file = root.text_or("driver_file", {});
  manifest.driver_url = root.text_or("driver_url", {});
  manifest.driver_sha256 = root.text_or("driver_sha256", {});
  if (const Json* devices = root.find("supported_device_ids");
      devices != nullptr && devices->kind == Json::Kind::Array) {
    for (const Json& item : devices->array) {
      if (item.kind == Json::Kind::Number && item.number > 0) {
        manifest.device_ids.push_back(static_cast<std::uint32_t>(item.number));
      }
    }
  }
  return true;
}

PrecheckResult evaluate_precheck(const HostFacts& host, const ReleaseManifest& manifest) {
  PrecheckResult result;
  result.vc_file = manifest.vc_file;
  result.vc_sha256 = manifest.vc_sha256;
  result.driver_file = manifest.driver_file;
  result.driver_url = manifest.driver_url;
  result.driver_sha256 = manifest.driver_sha256;
  if (!host.x64 || host.windows_build < manifest.windows_build_min) {
    result.message = "потрібна 64-бітна Windows 11";
    return result;
  }
  if (manifest.disk_bytes == 0 || host.free_bytes < manifest.disk_bytes) {
    result.message = "недостатньо місця на диску або маніфест не фіксує обсяг";
    return result;
  }
  if (host.app_running) {
    result.message = "rtx-vsr-proxy або rtx-vsr-gui уже запущено";
    return result;
  }
  if (manifest.vc_redist_build <= 0 || manifest.vc_file.empty() || manifest.vc_sha256.empty()) {
    result.message = "маніфест не фіксує офіційний VC++ Runtime x64 і його контрольну суму";
    return result;
  }
  if (!host.runtime_installed || host.runtime_build < manifest.vc_redist_build) {
    result.runtime = RuntimeChoice::Install;
  }
  if (host.nvidia_device_ids.empty()) {
    result.driver = DriverChoice::Absent;
    result.message = "немає адаптера NVIDIA";
    return result;
  }
  if (manifest.min_driver <= 0 || manifest.device_ids.empty()) {
    result.message = "маніфест не фіксує мінімальний драйвер і перелік GPU; драйвер не змінюється";
    return result;
  }
  for (const std::uint32_t id : host.nvidia_device_ids) {
    if (!listed(manifest.device_ids, id)) {
      result.driver = DriverChoice::Ambiguous;
      result.message = "адаптер NVIDIA не входить до переліку пакета; драйвер не встановлюється приблизно";
      return result;
    }
  }
  if (host.driver_present && host.driver_version >= manifest.min_driver) {
    result.driver = DriverChoice::Keep;
    result.proceed = true;
    result.message = "сумісний драйвер уже встановлено";
    return result;
  }
  result.driver = DriverChoice::InstallOfficial;
  const bool package =
      !manifest.driver_sha256.empty() && (!manifest.driver_file.empty() || !manifest.driver_url.empty());
  if (!package) {
    result.message = "потрібен офіційний підписаний пакет драйвера з контрольною сумою";
    return result;
  }
  result.proceed = true;
  result.message = "потрібно встановити офіційний драйвер NVIDIA";
  return result;
}

std::string precheck_json(const PrecheckResult& result) {
  std::ostringstream out;
  out << "{\"proceed\":" << (result.proceed ? "true" : "false") << ",\"runtime\":\"" << runtime_name(result.runtime)
      << "\",\"driver\":\"" << driver_name(result.driver) << "\",\"vc_file\":\"" << ascii_escape(result.vc_file)
      << "\",\"vc_sha256\":\"" << ascii_escape(result.vc_sha256) << "\",\"driver_file\":\""
      << ascii_escape(result.driver_file) << "\",\"driver_url\":\"" << ascii_escape(result.driver_url)
      << "\",\"driver_sha256\":\"" << ascii_escape(result.driver_sha256) << "\",\"message\":\""
      << ascii_escape(result.message) << "\"}";
  return out.str();
}

}  // namespace rtx
