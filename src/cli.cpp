#include "cli.hpp"

#include "catalog.hpp"
#include "dependencies.hpp"
#include "pipeline.hpp"
#include "profile.hpp"
#include "report.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

extern "C" {
#include <libavutil/log.h>
}

namespace rtx {
namespace {

void print_help() {
  std::cerr << R"(rtx-vsr-proxy — локальний відеопроксі NVDEC → RTX Video SDK → NVENC

Команди:
  capabilities [--json]     лише режими, які є в цій збірці FFmpeg
  doctor [--json]          GPU, NVENC і чи зібрано RTX Video SDK
  run [--profile файл]      проксі з окремими входом і виходом

Вхід і вихід задаються незалежно. Роль визначає, чий це порт:
  client       порт віддаленого вузла; local_port лише якщо протокол має джерельний порт
  listener     локальна адреса й порт прийому
  server       локальна адреса й порт, на якому застосунок або FFmpeg слухає
  rendezvous   режим SRT, якщо він є в бібліотеці

Приклади:
  rtx-vsr-proxy capabilities
  rtx-vsr-proxy run \
    --input-protocol udp --input-role listener --input-local-port 5600 \
    --output-protocol http --output-role server --output-local-port 8080
  rtx-vsr-proxy run \
    --input-protocol rtsp --input-role client --input-url rtsp://192.168.1.20:554/live \
    --output-protocol srt --output-role listener --output-local-port 9000

RTP: --input-payload і --output-payload mpegts (типово), h264, hevc або es.
Типовий MPEG-TS over RTP пише муксер rtp_mpegts (payload type 33) і читає демуксер rtp.
Елементарний вихід пише SDP у --output-sdp. Елементарний вхід читає --input-path файл.sdp.

Якість за замовчуванням адаптивна, старт з рівня 2. Масштаб 2 або 3.
Якщо масштаб 3 не вкладається в інтервал першого кадру, до відкриття виходу він падає до 2.
Профіль схеми 1: --profile файл.json. Лише адреса і порт, без імені користувача і пароля.
Карта: --gpu-uuid або --gpu N. Порівняння запису: --pace і --warmup секунди; явне --quality вимикає адаптацію.
Події стану й метрики — рядки JSON на stdout. Журнал FFmpeg лишається на stderr.
Команда зупинки на stdin: stop
Метрики містять час обробки проксі: proxy_p95_ms і proxy_p99_ms за кожним кадром вікна.
vsr_state: running, degraded, overloaded, bypass.
)";
}

bool parse_int(const std::string& text, int& value) {
  char* end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

bool parse_i64(const std::string& text, std::int64_t& value) {
  char* end = nullptr;
  const long long parsed = std::strtoll(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') {
    return false;
  }
  value = static_cast<std::int64_t>(parsed);
  return true;
}

bool parse_double(const std::string& text, double& value) {
  char* end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0';
}

bool take_arg(int& index, int argc, char** argv, std::string& value) {
  if (index + 1 >= argc) {
    return false;
  }
  value = argv[++index];
  return true;
}

bool assign_endpoint_flag(Endpoint& endpoint, const std::string& flag, const std::string& value) {
  if (flag == "protocol") {
    endpoint.protocol = value;
    return true;
  }
  if (flag == "role") {
    endpoint.role = value;
    return true;
  }
  if (flag == "url") {
    endpoint.url = value;
    return true;
  }
  if (flag == "path") {
    endpoint.path = value;
    return true;
  }
  if (flag == "remote-host") {
    endpoint.remote_host = value;
    return true;
  }
  if (flag == "remote-port") {
    return parse_int(value, endpoint.remote_port);
  }
  if (flag == "local-host") {
    endpoint.local_host = value;
    return true;
  }
  if (flag == "local-port") {
    return parse_int(value, endpoint.local_port);
  }
  if (flag == "app") {
    endpoint.app = value;
    return true;
  }
  if (flag == "playpath") {
    endpoint.playpath = value;
    return true;
  }
  if (flag == "rtp-port-min") {
    return parse_int(value, endpoint.rtp_port_min);
  }
  if (flag == "rtp-port-max") {
    return parse_int(value, endpoint.rtp_port_max);
  }
  if (flag == "rtcp-port") {
    return parse_int(value, endpoint.rtcp_port);
  }
  if (flag == "directory") {
    endpoint.directory = value;
    return true;
  }
  if (flag == "payload") {
    endpoint.payload = value;
    return true;
  }
  if (flag == "sdp") {
    endpoint.sdp = value;
    return true;
  }
  if (flag == "param") {
    const auto split = value.find('=');
    if (split == std::string::npos || split == 0) {
      return false;
    }
    endpoint.params.emplace_back(value.substr(0, split), value.substr(split + 1));
    return true;
  }
  return false;
}

std::string scheme_of(const std::string& url) {
  const auto pos = url.find("://");
  if (pos == std::string::npos) {
    return {};
  }
  return url.substr(0, pos);
}

void fill_role_from_url(Endpoint& endpoint) {
  if (!endpoint.role.empty() || endpoint.url.empty()) {
    return;
  }
  const auto query = endpoint.url.find('?');
  if (query != std::string::npos && endpoint.url.find("mode=listener", query) != std::string::npos) {
    endpoint.role = "listener";
  } else if (query != std::string::npos && endpoint.url.find("mode=rendezvous", query) != std::string::npos) {
    endpoint.role = "rendezvous";
  } else if (endpoint.protocol == "file") {
    endpoint.role = "client";
  } else if (endpoint.protocol == "udp" || endpoint.protocol == "udplite" || endpoint.protocol == "rtp") {
    endpoint.role = "listener";
  } else {
    endpoint.role = "client";
  }
}

}  // namespace

int cli_main(int argc, char** argv) {
  if (argc < 2) {
    print_help();
    return 2;
  }
  const std::string command = argv[1];
  if (command == "--help" || command == "-h" || command == "help") {
    print_help();
    return 0;
  }
  Settings settings;
  bool json = false;
  bool explicit_scale = false;
  bool explicit_quality = false;
  bool explicit_quality_min = false;
  bool explicit_quality_max = false;
  bool explicit_gpu = false;
  bool explicit_gpu_uuid = false;
  bool explicit_codec = false;
  bool explicit_bitrate = false;
  bool explicit_input = false;
  bool explicit_output = false;
  bool explicit_warmup = false;
  std::string profile_path;
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--json") {
      json = true;
      continue;
    }
    if (arg == "--verbose") {
      settings.verbose = true;
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    }
    std::string value;
    auto need = [&](const std::string& name) {
      if (!take_arg(index, argc, argv, value)) {
        std::cerr << "бракує значення для " << name << '\n';
        return false;
      }
      return true;
    };
    if (arg == "--profile") {
      if (!need(arg)) {
        return 2;
      }
      profile_path = value;
      continue;
    }
    if (arg == "--pace") {
      settings.pace_input = true;
      continue;
    }
    if (arg == "--warmup") {
      if (!need(arg) || !parse_double(value, settings.warmup_s) || settings.warmup_s < 0) {
        return 2;
      }
      explicit_warmup = true;
      continue;
    }
    if (arg == "--gpu-uuid") {
      if (!need(arg)) {
        return 2;
      }
      settings.gpu_uuid = value;
      explicit_gpu_uuid = true;
      continue;
    }
    if (arg == "--scale") {
      if (!need(arg) || !parse_int(value, settings.scale)) {
        return 2;
      }
      explicit_scale = true;
      continue;
    }
    if (arg == "--quality") {
      if (!need(arg)) {
        return 2;
      }
      if (value == "auto") {
        settings.quality_auto = true;
      } else if (!parse_int(value, settings.quality)) {
        std::cerr << "якість: auto або 1..4\n";
        return 2;
      } else {
        settings.quality_auto = false;
      }
      explicit_quality = true;
      continue;
    }
    if (arg == "--quality-min") {
      if (!need(arg) || !parse_int(value, settings.quality_min)) {
        return 2;
      }
      explicit_quality_min = true;
      continue;
    }
    if (arg == "--quality-max") {
      if (!need(arg) || !parse_int(value, settings.quality_max)) {
        return 2;
      }
      explicit_quality_max = true;
      continue;
    }
    if (arg == "--gpu") {
      if (!need(arg) || !parse_int(value, settings.gpu)) {
        return 2;
      }
      explicit_gpu = true;
      continue;
    }
    if (arg == "--codec") {
      if (!need(arg) || (value != "h264" && value != "hevc")) {
        std::cerr << "кодек: h264 або hevc\n";
        return 2;
      }
      settings.codec = value;
      explicit_codec = true;
      continue;
    }
    if (arg == "--bitrate") {
      if (!need(arg) || !parse_i64(value, settings.bitrate) || settings.bitrate <= 0) {
        return 2;
      }
      explicit_bitrate = true;
      continue;
    }
    if (arg == "--metrics-interval") {
      if (!need(arg) || !parse_double(value, settings.metrics_interval) || settings.metrics_interval < 0) {
        return 2;
      }
      continue;
    }
    if (arg == "--metrics-file") {
      if (!need(arg)) {
        return 2;
      }
      settings.metrics_file = value;
      continue;
    }
    if (arg == "--input-format") {
      if (!need(arg)) {
        return 2;
      }
      settings.input_format = value;
      continue;
    }
    if (arg == "--output-format") {
      if (!need(arg)) {
        return 2;
      }
      settings.output_format = value;
      continue;
    }
    const auto input_flag = arg.rfind("--input-", 0);
    const auto output_flag = arg.rfind("--output-", 0);
    if (input_flag != 0 && output_flag != 0) {
      std::cerr << "невідомий аргумент " << arg << '\n';
      return 2;
    }
    if (!need(arg)) {
      return 2;
    }
    const bool input = input_flag == 0;
    if (input) {
      explicit_input = true;
    } else {
      explicit_output = true;
    }
    const std::string flag = arg.substr(input ? 8 : 9);
    Endpoint& endpoint = input ? settings.input : settings.output;
    if (!assign_endpoint_flag(endpoint, flag, value)) {
      std::cerr << "некоректне значення " << arg << '\n';
      return 2;
    }
  }

  if (!profile_path.empty()) {
    Profile profile;
    std::string error;
    if (!load_profile_file(profile_path, profile, error)) {
      std::cerr << error << '\n';
      return 2;
    }
    if (const std::string denied = password_access_error(profile); !denied.empty()) {
      std::cerr << denied << '\n';
      return 2;
    }
    Settings loaded;
    apply_profile(profile, loaded);
    loaded.verbose = settings.verbose;
    loaded.metrics_interval = settings.metrics_interval;
    loaded.metrics_file = settings.metrics_file;
    loaded.input_format = settings.input_format;
    loaded.output_format = settings.output_format;
    loaded.pace_input = settings.pace_input;
    if (explicit_scale) {
      loaded.scale = settings.scale;
    }
    if (explicit_quality) {
      loaded.quality_auto = settings.quality_auto;
      loaded.quality = settings.quality;
    }
    if (explicit_quality_min) {
      loaded.quality_min = settings.quality_min;
    }
    if (explicit_quality_max) {
      loaded.quality_max = settings.quality_max;
    }
    if (explicit_gpu) {
      loaded.gpu = settings.gpu;
      loaded.gpu_uuid.clear();
    }
    if (explicit_gpu_uuid) {
      loaded.gpu_uuid = settings.gpu_uuid;
    }
    if (explicit_codec) {
      loaded.codec = settings.codec;
    }
    if (explicit_bitrate) {
      loaded.bitrate = settings.bitrate;
    }
    if (explicit_input) {
      loaded.input = settings.input;
    }
    if (explicit_output) {
      loaded.output = settings.output;
    }
    if (explicit_warmup) {
      loaded.warmup_s = settings.warmup_s;
    }
    settings = loaded;
  }

  if (settings.verbose) {
    av_log_set_level(AV_LOG_INFO);
  } else {
    av_log_set_level(AV_LOG_ERROR);
  }
  if (!settings.quality_auto) {
    settings.quality_min = settings.quality;
    settings.quality_max = settings.quality;
  }
  if (settings.quality < 1 || settings.quality > 4 || settings.quality_min < 1 || settings.quality_max > 4 ||
      settings.quality_min > settings.quality_max) {
    std::cerr << "якість RTX VSR має бути в діапазоні 1..4\n";
    return 2;
  }
  if (settings.scale != 2 && settings.scale != 3) {
    std::cerr << "масштаб RTX VSR: 2 або 3\n";
    return 2;
  }

  const std::vector<Mode> modes = discover_modes();
  if (command == "capabilities") {
    std::cout << (json ? modes_json(modes) : modes_text(modes));
    if (json) {
      std::cout << '\n';
    }
    return 0;
  }
  if (command == "doctor") {
    const DependencyReport report = inspect_dependencies();
    Settings probe_settings = settings;
    if (probe_settings.codec != "h264" && probe_settings.codec != "hevc") {
      probe_settings.codec = "h264";
    }
    const std::string missing_gpu = bind_gpu_selection(probe_settings, report);
    std::string blockers = dependency_blockers(probe_settings, report);
    if (!missing_gpu.empty()) {
      blockers += missing_gpu + "\n";
    }
    if (json) {
      std::cout << doctor_json(report, blockers, modes.size()) << '\n';
      return blockers.empty() ? 0 : 3;
    }
    std::cout << (blockers.empty() ? report.sdk_note + "\n" + report.cuda_note + "\n" : blockers);
    std::cout << "h264_nvenc: " << (report.nvenc_h264 ? "так" : "ні") << '\n';
    std::cout << "hevc_nvenc: " << (report.nvenc_hevc ? "так" : "ні") << '\n';
    for (const GpuInfo& gpu : report.gpus) {
      std::cout << "GPU " << gpu.index << " " << gpu.name << " " << gpu.uuid << " CC " << gpu.cc_major << '.'
                << gpu.cc_minor << '\n';
    }
    std::cout << "режимів: " << modes.size() << '\n';
    if (!blockers.empty()) {
      std::cout << "ефект RTX VSR: не підтверджено\n";
      return 3;
    }
    std::cout << "ефект RTX VSR: створення перевірено\n";
    return 0;
  }
  if (command != "run") {
    print_help();
    return 2;
  }
  if (settings.input.protocol.empty() && !settings.input.url.empty()) {
    settings.input.protocol = scheme_of(settings.input.url);
  }
  if (settings.output.protocol.empty() && !settings.output.url.empty()) {
    settings.output.protocol = scheme_of(settings.output.url);
  }
  if (settings.input.protocol == "file" && settings.input.role.empty()) {
    settings.input.role = "client";
  }
  if (settings.output.protocol == "file" && settings.output.role.empty()) {
    settings.output.role = "client";
  }
  fill_role_from_url(settings.input);
  if (settings.output.role.empty() && !settings.output.url.empty()) {
    const auto query = settings.output.url.find('?');
    if (query != std::string::npos && settings.output.url.find("mode=listener", query) != std::string::npos) {
      settings.output.role = "listener";
    } else if (settings.output.protocol == "udp" || settings.output.protocol == "udplite" ||
               settings.output.protocol == "rtp") {
      settings.output.role = "client";
    } else {
      settings.output.role = "client";
    }
  }
  if (settings.input.protocol.empty() || settings.input.role.empty() || settings.output.protocol.empty() ||
      settings.output.role.empty()) {
    std::cerr << "потрібні протокол і роль входу та виходу\n";
    return 2;
  }
  return run_proxy(settings, modes, ffmpeg_probe());
}

}  // namespace rtx
