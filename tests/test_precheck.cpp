#include "check.hpp"
#include "precheck_rules.hpp"

#include <string>

namespace {

rtx::HostFacts ready_host() {
  rtx::HostFacts host;
  host.windows_build = 22631;
  host.x64 = true;
  host.free_bytes = 5000;
  host.runtime_installed = true;
  host.runtime_build = 40000;
  host.nvidia_device_ids.push_back(0x2684);
  host.driver_present = true;
  host.driver_version = 57216;
  return host;
}

const char* kManifest =
    R"({"windows_build_min":22000,"disk_bytes":1000,"vc_redist_build":30000,"vc_file":"vc_redist.x64.exe","vc_sha256":"abc","min_driver":56094,"supported_device_ids":[9860],"driver_file":"nvidia-driver.exe","driver_sha256":"def"})";

rtx::ReleaseManifest manifest() {
  rtx::ReleaseManifest value;
  std::string error;
  CHECK(rtx::parse_release_manifest(kManifest, value, error));
  return value;
}

}  // namespace

int main() {
  CHECK(rtx::nvidia_driver_number(15, 7216) == 57216);
  CHECK(rtx::nvidia_driver_number(15, 6094) == 56094);
  CHECK(rtx::nvidia_driver_number(9, 100) == 0);

  std::uint32_t device = 0;
  CHECK(rtx::nvidia_device_id("PCI\\VEN_10DE&DEV_2684&SUBSYS_0000", device));
  CHECK(device == 0x2684);
  CHECK(rtx::nvidia_device_id("PCI\\VEN_10de&DEV_abcd", device));
  CHECK(device == 0xABCD);
  CHECK(!rtx::nvidia_device_id("PCI\\VEN_1002&DEV_2684", device));

  rtx::ReleaseManifest parsed = manifest();
  CHECK(parsed.min_driver == 56094);
  CHECK(parsed.device_ids.size() == 1);
  CHECK(parsed.device_ids[0] == 9860);

  rtx::HostFacts host = ready_host();
  rtx::PrecheckResult result = rtx::evaluate_precheck(host, parsed);
  CHECK(result.proceed);
  CHECK(result.runtime == rtx::RuntimeChoice::Keep);
  CHECK(result.driver == rtx::DriverChoice::Keep);
  const std::string json = rtx::precheck_json(result);
  CHECK(json.find("\"driver\":\"keep\"") != std::string::npos);
  CHECK(json.find("\\u") != std::string::npos);
  for (const unsigned char symbol : json) {
    CHECK(symbol < 128);
  }

  host.runtime_build = 100;
  result = rtx::evaluate_precheck(host, parsed);
  CHECK(result.proceed);
  CHECK(result.runtime == rtx::RuntimeChoice::Install);
  CHECK(result.driver == rtx::DriverChoice::Keep);

  host = ready_host();
  host.runtime_installed = false;
  result = rtx::evaluate_precheck(host, parsed);
  CHECK(result.runtime == rtx::RuntimeChoice::Install);
  CHECK(result.proceed);

  host = ready_host();
  host.windows_build = 19045;
  result = rtx::evaluate_precheck(host, parsed);
  CHECK(!result.proceed);

  host = ready_host();
  host.x64 = false;
  CHECK(!rtx::evaluate_precheck(host, parsed).proceed);

  host = ready_host();
  host.free_bytes = 10;
  CHECK(!rtx::evaluate_precheck(host, parsed).proceed);

  host = ready_host();
  host.app_running = true;
  CHECK(!rtx::evaluate_precheck(host, parsed).proceed);

  host = ready_host();
  host.nvidia_device_ids.clear();
  result = rtx::evaluate_precheck(host, parsed);
  CHECK(!result.proceed);
  CHECK(result.driver == rtx::DriverChoice::Absent);

  host = ready_host();
  host.nvidia_device_ids.push_back(0x2204);
  result = rtx::evaluate_precheck(host, parsed);
  CHECK(!result.proceed);
  CHECK(result.driver == rtx::DriverChoice::Ambiguous);
  CHECK(rtx::precheck_json(result).find("\"driver\":\"ambiguous\"") != std::string::npos);

  host = ready_host();
  host.driver_present = false;
  host.driver_version = 0;
  result = rtx::evaluate_precheck(host, parsed);
  CHECK(result.proceed);
  CHECK(result.driver == rtx::DriverChoice::InstallOfficial);

  rtx::ReleaseManifest no_package = parsed;
  no_package.driver_file.clear();
  no_package.driver_sha256.clear();
  host = ready_host();
  host.driver_present = false;
  result = rtx::evaluate_precheck(host, no_package);
  CHECK(!result.proceed);
  CHECK(result.driver == rtx::DriverChoice::InstallOfficial);

  rtx::ReleaseManifest unset = parsed;
  unset.device_ids.clear();
  unset.min_driver = 0;
  host = ready_host();
  result = rtx::evaluate_precheck(host, unset);
  CHECK(!result.proceed);
  CHECK(result.message.find("не змінюється") != std::string::npos);

  std::string error;
  rtx::ReleaseManifest rejected;
  CHECK(!rtx::parse_release_manifest("[]", rejected, error));
  const std::string bom = std::string("\xEF\xBB\xBF") + kManifest;
  CHECK(rtx::parse_release_manifest(bom, rejected, error));
  CHECK(rejected.min_driver == 56094);
  return rtx_test::result();
}
