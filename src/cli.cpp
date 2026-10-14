#include "cli.hpp"

#include "catalog.hpp"
#include "dependencies.hpp"
#include "pipeline.hpp"

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
  doctor                    GPU, NVENC і чи зібрано RTX Video SDK
  run                       проксі з окремими входом і виходом

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

Якість за замовчуванням адаптивна, старт з рівня 2. Масштаб 2 або 3.
Метрики йдуть у stderr як JSON: fps, frames_dropped, last_process_ms, vsr_quality, vsr_state.
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
    if (arg == "--scale") {
      if (!need(arg) || !parse_int(value, settings.scale)) {
        return 2;
      }
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
      continue;
    }
    if (arg == "--quality-min") {
      if (!need(arg) || !parse_int(value, settings.quality_min)) {
        return 2;
      }
      continue;
    }
    if (arg == "--quality-max") {
      if (!need(arg) || !parse_int(value, settings.quality_max)) {
        return 2;
      }
      continue;
    }
    if (arg == "--gpu") {
      if (!need(arg) || !parse_int(value, settings.gpu)) {
        return 2;
      }
      continue;
    }
    if (arg == "--codec") {
      if (!need(arg) || (value != "h264" && value != "hevc")) {
        std::cerr << "кодек: h264 або hevc\n";
        return 2;
      }
      settings.codec = value;
      continue;
    }
    if (arg == "--bitrate") {
      if (!need(arg) || !parse_i64(value, settings.bitrate) || settings.bitrate <= 0) {
        return 2;
      }
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
    const std::string flag = arg.substr(input ? 8 : 9);
    Endpoint& endpoint = input ? settings.input : settings.output;
    if (!assign_endpoint_flag(endpoint, flag, value)) {
      std::cerr << "некоректне значення " << arg << '\n';
      return 2;
    }
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
    std::cout << report.sdk_note << '\n' << report.cuda_note << '\n';
    std::cout << "h264_nvenc: " << (report.nvenc_h264 ? "так" : "ні") << '\n';
    std::cout << "hevc_nvenc: " << (report.nvenc_hevc ? "так" : "ні") << '\n';
    for (const GpuInfo& gpu : report.gpus) {
      std::cout << "GPU " << gpu.index << " " << gpu.name << " CC " << gpu.cc_major << '.' << gpu.cc_minor << '\n';
    }
    std::cout << "режимів: " << modes.size() << '\n';
    return report.sdk_built && !report.gpus.empty() && report.nvenc_h264 ? 0 : 3;
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
