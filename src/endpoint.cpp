#include "endpoint.hpp"

#include <cctype>
#include <filesystem>
#include <map>
#include <sstream>
#include <system_error>

namespace rtx {
namespace {

bool is_set_string(const std::string& value) { return !value.empty(); }

int default_port(const std::string& protocol) {
  if (protocol == "http" || protocol == "mmsh" || protocol == "mmst" || protocol == "hls") {
    return 80;
  }
  if (protocol == "https") {
    return 443;
  }
  if (protocol == "rtsp") {
    return 554;
  }
  if (protocol == "rtmp" || protocol == "rtmps" || protocol == "rtmpt" || protocol == "rtmpts") {
    return 1935;
  }
  return -1;
}

bool field_set(const Endpoint& endpoint, const std::string& name) {
  if (name == "path") {
    return is_set_string(endpoint.path);
  }
  if (name == "url") {
    return is_set_string(endpoint.url);
  }
  if (name == "remote_host") {
    return is_set_string(endpoint.remote_host);
  }
  if (name == "remote_port") {
    return endpoint.remote_port > 0;
  }
  if (name == "local_host") {
    return is_set_string(endpoint.local_host);
  }
  if (name == "local_port") {
    return endpoint.local_port > 0;
  }
  if (name == "app") {
    return is_set_string(endpoint.app);
  }
  if (name == "playpath") {
    return is_set_string(endpoint.playpath);
  }
  if (name == "rtp_port_min") {
    return endpoint.rtp_port_min > 0;
  }
  if (name == "rtp_port_max") {
    return endpoint.rtp_port_max > 0;
  }
  if (name == "rtcp_port") {
    return endpoint.rtcp_port > 0;
  }
  if (name == "directory") {
    return is_set_string(endpoint.directory);
  }
  if (name == "sdp") {
    return is_set_string(endpoint.sdp);
  }
  if (name == "params") {
    return !endpoint.params.empty();
  }
  return false;
}

bool listed(const std::vector<std::string>& names, const std::string& name) {
  for (const std::string& item : names) {
    if (item == name) {
      return true;
    }
  }
  return false;
}

bool allowed_field(const Mode& mode, const std::string& name) {
  return listed(mode.required, name) || listed(mode.optional, name);
}

std::string location_error(int port, const std::string& label) {
  if (port == -1) {
    return {};
  }
  if (port < 1 || port > 65535) {
    return label + " має бути в діапазоні 1..65535";
  }
  return {};
}

std::string scheme_of(const std::string& url) {
  const auto pos = url.find("://");
  if (pos == std::string::npos || pos == 0) {
    return {};
  }
  return url.substr(0, pos);
}

std::string hostport(const std::string& host, int port) {
  if (host.find(':') != std::string::npos) {
    return "[" + host + "]:" + std::to_string(port);
  }
  return host + ":" + std::to_string(port);
}

std::string percent_encode(const std::string& text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (unsigned char ch : text) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/') {
      out += static_cast<char>(ch);
    } else {
      out += '%';
      out += kHex[ch >> 4];
      out += kHex[ch & 15];
    }
  }
  return out;
}

std::string with_slash(std::string path) {
  if (path.empty()) {
    return "/";
  }
  if (path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  return path;
}

void put_default(std::map<std::string, std::string>& values, const std::string& key, const std::string& value) {
  if (values.find(key) == values.end()) {
    values.emplace(key, value);
  }
}

std::string join_query(const std::string& base, const std::map<std::string, std::string>& query) {
  if (query.empty()) {
    return base;
  }
  std::ostringstream out;
  out << base << (base.find('?') == std::string::npos ? '?' : '&');
  bool first = true;
  for (const auto& item : query) {
    if (!first) {
      out << '&';
    }
    first = false;
    out << percent_encode(item.first) << '=' << percent_encode(item.second);
  }
  return out.str();
}

bool is_output(const Mode& mode) { return mode.direction == Direction::Output; }

bool legacy_query(const std::string& protocol) {
  return protocol == "udp" || protocol == "udplite" || protocol == "srt" || protocol == "rtp";
}

void place_protocol(const std::string& protocol, std::map<std::string, std::string>& query,
                    std::map<std::string, std::string>& protocol_options, const std::string& key,
                    const std::string& value) {
  protocol_options[key] = value;
  if (legacy_query(protocol)) {
    query[key] = value;
  }
}

std::string apply_user_params(const Endpoint& endpoint, const Mode& mode, const OptionProbe& probe,
                              std::map<std::string, std::string>& query,
                              std::map<std::string, std::string>& protocol_options,
                              std::map<std::string, std::string>& format_options) {
  const bool output = is_output(mode);
  const std::string& format_name = mode.ffmpeg_name.empty() ? mode.protocol : mode.ffmpeg_name;
  for (const auto& param : endpoint.params) {
    if (param.first.empty()) {
      return "порожнє ім'я параметра";
    }
    if (probe.protocol_option(mode.protocol, param.first, output)) {
      place_protocol(mode.protocol, query, protocol_options, param.first, param.second);
      continue;
    }
    if (probe.format_option(format_name, param.first, output) ||
        (!mode.container.empty() && mode.container != format_name &&
         probe.format_option(mode.container, param.first, output))) {
      format_options[param.first] = param.second;
      continue;
    }
    return "параметр \"" + param.first + "\" недоступний для " + mode.protocol + " у напрямку " +
           to_string(mode.direction);
  }
  return {};
}

void add_protocol_default(const OptionProbe& probe, const Mode& mode, const std::string& key,
                          const std::string& value, std::map<std::string, std::string>& query,
                          std::map<std::string, std::string>& protocol_options) {
  if (probe.protocol_option(mode.protocol, key, is_output(mode)) && protocol_options.find(key) == protocol_options.end()) {
    place_protocol(mode.protocol, query, protocol_options, key, value);
  }
}

void add_format_default(const OptionProbe& probe, const Mode& mode, const std::string& key,
                        const std::string& value, std::map<std::string, std::string>& format_options) {
  const std::string& format_name = mode.ffmpeg_name.empty() ? mode.protocol : mode.ffmpeg_name;
  if (probe.format_option(format_name, key, is_output(mode))) {
    put_default(format_options, key, value);
  }
}

std::string local_bind_host(const Endpoint& endpoint, Role role) {
  if (!endpoint.local_host.empty()) {
    return endpoint.local_host;
  }
  if (role == Role::Server) {
    return "127.0.0.1";
  }
  return "0.0.0.0";
}

int resolved_remote_port(const Endpoint& endpoint) {
  if (endpoint.remote_port > 0) {
    return endpoint.remote_port;
  }
  return default_port(endpoint.protocol);
}

std::string build_location_url(const Endpoint& endpoint, const Mode& mode, std::string& error) {
  if (mode.container == "sdp") {
    return endpoint.path;
  }
  if (!endpoint.url.empty()) {
    return endpoint.url;
  }
  const std::string proto = endpoint.protocol;
  if (proto == "file") {
    return endpoint.path;
  }
  if (proto == "hls" && mode.role == Role::Server) {
    return {};
  }
  if (mode.role == Role::Listener || mode.role == Role::Server) {
    if (endpoint.local_port < 1) {
      error = "потрібен локальний порт";
      return {};
    }
    const std::string host = local_bind_host(endpoint, mode.role);
    std::string path;
    if (proto == "rtmp" || proto == "rtmps" || proto == "rtmpt" || proto == "rtmpts") {
      path = "/" + endpoint.app + "/" + endpoint.playpath;
    } else if (proto == "http" || proto == "https" || proto == "rtsp" || proto == "mmsh" || proto == "mmst") {
      path = with_slash(endpoint.path);
    }
    return proto + "://" + hostport(host, endpoint.local_port) + path;
  }
  const int port = resolved_remote_port(endpoint);
  if (endpoint.remote_host.empty() || port < 1) {
    error = "потрібні адреса і порт призначення";
    return {};
  }
  std::string path;
  if (proto == "rtmp" || proto == "rtmps" || proto == "rtmpt" || proto == "rtmpts") {
    path = "/" + endpoint.app + "/" + endpoint.playpath;
  } else if (proto == "http" || proto == "https" || proto == "rtsp" || proto == "hls" || proto == "mmsh" ||
             proto == "mmst") {
    path = endpoint.path.empty() ? std::string{} : with_slash(endpoint.path);
  }
  return proto + "://" + hostport(endpoint.remote_host, port) + path;
}

}  // namespace

const Mode* find_mode(const std::vector<Mode>& modes, Direction direction, const std::string& protocol,
                      const std::string& role_text, const std::string& container) {
  Role role{};
  if (!parse_role(role_text, role)) {
    return nullptr;
  }
  const Mode* found = nullptr;
  for (const Mode& mode : modes) {
    if (mode.direction != direction || mode.protocol != protocol || mode.role != role) {
      continue;
    }
    if (!container.empty()) {
      if (mode.container == container) {
        return &mode;
      }
      continue;
    }
    const auto preference = [](const std::string& name) {
      if (name == "rtp_mpegts") {
        return 2;
      }
      if (name == "mpegts") {
        return 1;
      }
      return 0;
    };
    if (found == nullptr || preference(mode.container) > preference(found->container)) {
      found = &mode;
    }
  }
  return found;
}

std::string validate_endpoint(const Endpoint& endpoint, const Mode& mode) {
  if (endpoint.protocol != mode.protocol) {
    return "протокол не збігається з режимом";
  }
  Role parsed{};
  if (!parse_role(endpoint.role, parsed) || parsed != mode.role) {
    return "роль не збігається з режимом";
  }
  const std::string port_error = location_error(endpoint.remote_port, "віддалений порт");
  if (!port_error.empty()) {
    return port_error;
  }
  const std::string local_error = location_error(endpoint.local_port, "локальний порт");
  if (!local_error.empty()) {
    return local_error;
  }
  const std::string rtp_min = location_error(endpoint.rtp_port_min, "мінімальний RTP-порт");
  if (!rtp_min.empty()) {
    return rtp_min;
  }
  const std::string rtp_max = location_error(endpoint.rtp_port_max, "максимальний RTP-порт");
  if (!rtp_max.empty()) {
    return rtp_max;
  }
  const std::string rtcp = location_error(endpoint.rtcp_port, "RTCP-порт");
  if (!rtcp.empty()) {
    return rtcp;
  }
  if ((endpoint.rtp_port_min > 0) != (endpoint.rtp_port_max > 0)) {
    return "RTP-діапазон задається парою rtp_port_min і rtp_port_max";
  }
  if (endpoint.rtp_port_min > 0 && endpoint.rtp_port_min > endpoint.rtp_port_max) {
    return "rtp_port_min більший за rtp_port_max";
  }
  if (!endpoint.payload.empty() && endpoint.protocol != "rtp") {
    return "payload використовується лише для rtp";
  }
  if (!endpoint.payload.empty() && endpoint.payload != "mpegts" && endpoint.payload != "h264" &&
      endpoint.payload != "hevc" && endpoint.payload != "es") {
    return "payload для rtp: mpegts, h264, hevc або es";
  }

  const char* location_names[] = {"path",     "remote_host", "remote_port", "local_host",   "local_port",
                                  "app",      "playpath",    "directory",   "rtp_port_min", "rtp_port_max",
                                  "rtcp_port", "sdp"};
  if (!endpoint.params.empty() && !allowed_field(mode, "params")) {
    return "параметри підключення не використовуються в цьому режимі";
  }
  if (!endpoint.url.empty()) {
    if (!mode.url_satisfies_location) {
      return "цей режим не приймає готовий URL";
    }
    const std::string scheme = scheme_of(endpoint.url);
    if (scheme != endpoint.protocol) {
      return "схема URL не збігається з протоколом";
    }
    for (const char* name : location_names) {
      if (field_set(endpoint, name)) {
        return std::string("поле ") + name + " конфліктує з URL";
      }
    }
    return {};
  }

  for (const std::string& name : mode.required) {
    if (name == "url") {
      continue;
    }
    if (!field_set(endpoint, name)) {
      const int implied = (name == "remote_port") ? default_port(endpoint.protocol) : -1;
      if (implied > 0 && is_set_string(endpoint.remote_host)) {
        continue;
      }
      return "бракує поля " + name;
    }
  }
  for (const char* name : location_names) {
    if (field_set(endpoint, name) && !allowed_field(mode, name)) {
      return std::string("поле ") + name + " не використовується в цьому режимі";
    }
  }
  if (endpoint.protocol == "file" && mode.direction == Direction::Input) {
    std::error_code error;
    const std::filesystem::path file(endpoint.path);
    if (!std::filesystem::is_regular_file(file, error) || error) {
      return "вхідний файл не знайдено";
    }
  }
  if (mode.container == "sdp" && mode.direction == Direction::Input) {
    std::error_code error;
    const std::filesystem::path file(endpoint.path);
    if (!std::filesystem::is_regular_file(file, error) || error) {
      return "файл SDP не знайдено";
    }
  }
  return {};
}

std::string build_endpoint(const Endpoint& endpoint, const Mode& mode, const OptionProbe& probe,
                           BuiltEndpoint& built) {
  const std::string invalid = validate_endpoint(endpoint, mode);
  if (!invalid.empty()) {
    return invalid;
  }
  built = BuiltEndpoint{};
  built.format = mode.container;
  std::map<std::string, std::string> query;
  std::map<std::string, std::string> protocol_options;
  std::string error;
  std::string url = build_location_url(endpoint, mode, error);
  if (!error.empty()) {
    return error;
  }

  if (endpoint.protocol == "udp" || endpoint.protocol == "udplite") {
    if (mode.direction == Direction::Input) {
      add_protocol_default(probe, mode, "overrun_nonfatal", "1", query, protocol_options);
      add_protocol_default(probe, mode, "fifo_size", "64", query, protocol_options);
      add_protocol_default(probe, mode, "reuse", "1", query, protocol_options);
    } else {
      add_protocol_default(probe, mode, "pkt_size", "1316", query, protocol_options);
      if (endpoint.local_port > 0) {
        add_protocol_default(probe, mode, "localport", std::to_string(endpoint.local_port), query, protocol_options);
      }
      if (!endpoint.local_host.empty()) {
        add_protocol_default(probe, mode, "localaddr", endpoint.local_host, query, protocol_options);
      }
    }
  } else if (endpoint.protocol == "srt") {
    if (mode.role == Role::Rendezvous && endpoint.url.empty() && endpoint.local_port != resolved_remote_port(endpoint)) {
      return "SRT rendezvous у цій збірці використовує один порт і для локальної прив'язки, і для віддаленого вузла";
    }
    if (mode.role == Role::Client && endpoint.local_port > 0) {
      return "SRT caller не має окремого локального порту в цій бібліотеці";
    }
    const char* srt_mode = "caller";
    if (mode.role == Role::Listener) {
      srt_mode = "listener";
    } else if (mode.role == Role::Rendezvous) {
      srt_mode = "rendezvous";
    }
    add_protocol_default(probe, mode, "mode", srt_mode, query, protocol_options);
    add_protocol_default(probe, mode, "latency", "20000", query, protocol_options);
    add_protocol_default(probe, mode, "tlpktdrop", "1", query, protocol_options);
    add_protocol_default(probe, mode, "transtype", "live", query, protocol_options);
  } else if (endpoint.protocol == "tcp") {
    if (mode.role == Role::Listener || mode.role == Role::Server) {
      add_protocol_default(probe, mode, "listen", "1", query, protocol_options);
    }
    add_protocol_default(probe, mode, "tcp_nodelay", "1", query, protocol_options);
  } else if (endpoint.protocol == "http" || endpoint.protocol == "https") {
    if (mode.role == Role::Listener || mode.role == Role::Server) {
      add_protocol_default(probe, mode, "listen", "1", query, protocol_options);
    }
  } else if (endpoint.protocol == "rtp" && mode.container != "sdp") {
    if (mode.container == "rtp") {
      built.sdp_path = endpoint.sdp;
    }
    if (endpoint.rtcp_port > 0) {
      add_protocol_default(probe, mode, "rtcp_port", std::to_string(endpoint.rtcp_port), query, protocol_options);
    }
    if (mode.direction == Direction::Output && endpoint.local_port > 0) {
      add_protocol_default(probe, mode, "local_rtpport", std::to_string(endpoint.local_port), query, protocol_options);
    }
  } else if (endpoint.protocol == "rtmp" || endpoint.protocol == "rtmps" || endpoint.protocol == "rtmpt" ||
             endpoint.protocol == "rtmpts") {
    if (mode.direction == Direction::Input) {
      add_protocol_default(probe, mode, "rtmp_live", "-1", query, protocol_options);
      add_protocol_default(probe, mode, "rtmp_buffer", "0", query, protocol_options);
      if (mode.role == Role::Listener) {
        add_protocol_default(probe, mode, "rtmp_listen", "1", query, protocol_options);
      }
    }
  } else if (endpoint.protocol == "rtsp") {
    if (mode.direction == Direction::Input) {
      add_format_default(probe, mode, "reorder_queue_size", "0", built.format_options);
      add_format_default(probe, mode, "stimeout", "5000000", built.format_options);
      if (mode.role == Role::Listener) {
        add_format_default(probe, mode, "rtsp_flags", "listen", built.format_options);
      }
    }
    if (endpoint.rtp_port_min > 0) {
      add_format_default(probe, mode, "min_port", std::to_string(endpoint.rtp_port_min), built.format_options);
      add_format_default(probe, mode, "max_port", std::to_string(endpoint.rtp_port_max), built.format_options);
    }
  } else if (endpoint.protocol == "hls" && mode.role == Role::Server) {
    built.application_hls = true;
    built.serve_host = local_bind_host(endpoint, Role::Server);
    built.serve_port = endpoint.local_port;
    built.serve_root = endpoint.directory.empty() ? std::string{} : endpoint.directory;
    built.public_url = "http://" + hostport(built.serve_host, built.serve_port) + "/index.m3u8";
    built.format = "hls";
  }

  const std::string params_error =
      apply_user_params(endpoint, mode, probe, query, protocol_options, built.format_options);
  if (!params_error.empty()) {
    return params_error;
  }
  built.protocol_options = std::move(protocol_options);
  if (!(endpoint.protocol == "hls" && mode.role == Role::Server && endpoint.url.empty())) {
    built.ffmpeg_url = join_query(url, query);
  }
  if (built.public_url.empty()) {
    built.public_url = built.ffmpeg_url;
  }
  return {};
}

std::string port_caption(Direction direction, Role role) {
  if (direction == Direction::Input && role == Role::Listener) {
    return "локальний порт прийому";
  }
  if (direction == Direction::Input && role == Role::Client) {
    return "порт джерела";
  }
  if (direction == Direction::Output && role == Role::Client) {
    return "порт отримувача";
  }
  if (direction == Direction::Output && (role == Role::Server || role == Role::Listener)) {
    return "локальний порт сервера";
  }
  if (role == Role::Rendezvous) {
    return "порт rendezvous";
  }
  return "порт";
}

std::string consumer_address(const Endpoint& endpoint) {
  std::string url = endpoint.url;
  const auto scheme = url.find("://");
  if (scheme != std::string::npos) {
    const auto authority = scheme + 3;
    const auto slash = url.find('/', authority);
    const auto at = url.find('@', authority);
    if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
      url = url.substr(0, authority) + url.substr(at + 1);
    }
    return url;
  }
  const bool local = endpoint.role == "server" || endpoint.role == "listener" || endpoint.role == "rendezvous";
  const std::string host = local ? (endpoint.local_host.empty() ? "127.0.0.1" : endpoint.local_host) : endpoint.remote_host;
  const int port = local ? endpoint.local_port : endpoint.remote_port;
  if (endpoint.protocol.empty() || host.empty() || port < 1) {
    return {};
  }
  url = endpoint.protocol + "://" + host + ":" + std::to_string(port);
  if (!endpoint.path.empty()) {
    if (endpoint.path.front() != '/') {
      url += '/';
    }
    url += endpoint.path;
  }
  return url;
}

}  // namespace rtx
