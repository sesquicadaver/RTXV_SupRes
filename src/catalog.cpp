#include "catalog.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
const AVClass* avio_protocol_get_class(const char* name);
}

namespace rtx {
namespace {

struct Library {
  std::set<std::string> input_protocols;
  std::set<std::string> output_protocols;
  std::map<std::string, const AVClass*> protocol_class;
  std::map<std::string, const AVInputFormat*> demuxers;
  std::map<std::string, const AVOutputFormat*> muxers;
};

const AVClass* protocol_class(const Library& library, const std::string& name) {
  const auto found = library.protocol_class.find(name);
  if (found == library.protocol_class.end()) {
    return nullptr;
  }
  return found->second;
}

bool class_option(const AVClass* cls, const std::string& name, bool output) {
  if (cls == nullptr) {
    return false;
  }
  const AVClass* fake = cls;
  const AVOption* option = av_opt_find(&fake, name.c_str(), nullptr, 0, AV_OPT_SEARCH_FAKE_OBJ);
  if (option == nullptr || option->type == AV_OPT_TYPE_CONST) {
    return false;
  }
  const int flag = output ? AV_OPT_FLAG_ENCODING_PARAM : AV_OPT_FLAG_DECODING_PARAM;
  return (option->flags & flag) != 0;
}

bool class_const(const AVClass* cls, const std::string& unit, const std::string& name, bool output) {
  if (cls == nullptr) {
    return false;
  }
  const AVClass* fake = cls;
  const AVOption* option = nullptr;
  const int flag = output ? AV_OPT_FLAG_ENCODING_PARAM : AV_OPT_FLAG_DECODING_PARAM;
  while ((option = av_opt_next(&fake, option)) != nullptr) {
    if (option->unit != nullptr && unit == option->unit && name == option->name) {
      return (option->flags & flag) != 0;
    }
  }
  return false;
}

Library scan_library() {
  Library library;
  avformat_network_init();
  void* opaque = nullptr;
  while (const char* name = avio_enum_protocols(&opaque, 0)) {
    library.input_protocols.emplace(name);
  }
  opaque = nullptr;
  while (const char* name = avio_enum_protocols(&opaque, 1)) {
    library.output_protocols.emplace(name);
  }
  using GetClass = const AVClass* (*)(const char*);
  GetClass get_class = avio_protocol_get_class;
  if (get_class != nullptr) {
    for (const std::string& name : library.input_protocols) {
      if (const AVClass* cls = get_class(name.c_str())) {
        library.protocol_class.emplace(name, cls);
      }
    }
    for (const std::string& name : library.output_protocols) {
      if (const AVClass* cls = get_class(name.c_str())) {
        library.protocol_class.emplace(name, cls);
      }
    }
  }
  opaque = nullptr;
  while (const AVInputFormat* format = av_demuxer_iterate(&opaque)) {
    if (format->name != nullptr) {
      library.demuxers.emplace(format->name, format);
    }
  }
  opaque = nullptr;
  while (const AVOutputFormat* format = av_muxer_iterate(&opaque)) {
    if (format->name != nullptr) {
      library.muxers.emplace(format->name, format);
    }
  }
  return library;
}

const Library& library() {
  static const Library value = scan_library();
  return value;
}

const AVClass* demuxer_class(const std::string& name) {
  const auto found = library().demuxers.find(name);
  if (found == library().demuxers.end() || found->second->priv_class == nullptr) {
    return nullptr;
  }
  return found->second->priv_class;
}

const AVClass* muxer_class(const std::string& name) {
  const auto found = library().muxers.find(name);
  if (found == library().muxers.end() || found->second->priv_class == nullptr) {
    return nullptr;
  }
  return found->second->priv_class;
}

class FfmpegProbe final : public OptionProbe {
 public:
  bool protocol_option(const std::string& protocol, const std::string& name, bool output) const override {
    return class_option(protocol_class(library(), protocol), name, output);
  }

  bool format_option(const std::string& format, const std::string& name, bool output) const override {
    const AVClass* cls = output ? muxer_class(format) : demuxer_class(format);
    return class_option(cls, name, output);
  }
};

bool has_protocol(Direction direction, const std::string& name) {
  const Library& lib = library();
  const auto& set = direction == Direction::Input ? lib.input_protocols : lib.output_protocols;
  return set.find(name) != set.end();
}

Mode make_mode(std::string protocol, Direction direction, Role role, std::string provider, std::string ffmpeg_name,
               std::string container, std::vector<std::string> required, std::vector<std::string> optional,
               bool url_ok, std::string summary) {
  Mode mode;
  mode.protocol = std::move(protocol);
  mode.direction = direction;
  mode.role = role;
  mode.provider = std::move(provider);
  mode.ffmpeg_name = std::move(ffmpeg_name);
  mode.container = std::move(container);
  mode.required = std::move(required);
  mode.optional = std::move(optional);
  mode.url_satisfies_location = url_ok;
  mode.summary = std::move(summary);
  return mode;
}

void add_udp_like(std::vector<Mode>& modes, const std::string& protocol) {
  if (has_protocol(Direction::Input, protocol)) {
    modes.push_back(make_mode(protocol, Direction::Input, Role::Listener, "ffmpeg-protocol", protocol, "mpegts",
                              {"local_port"}, {"local_host", "params"}, true,
                              "Прийом на локальній адресі й порту. Локальний порт — це порт прив'язки."));
  }
  if (has_protocol(Direction::Output, protocol)) {
    modes.push_back(make_mode(protocol, Direction::Output, Role::Client, "ffmpeg-protocol", protocol, "mpegts",
                              {"remote_host", "remote_port"}, {"local_host", "local_port", "params"}, true,
                              "Надсилання на адресу й порт отримувача. local_port — джерельний порт, якщо протокол його приймає."));
  }
}

void add_srt(std::vector<Mode>& modes) {
  const AVClass* cls = protocol_class(library(), "srt");
  if (cls == nullptr) {
    return;
  }
  const auto add = [&](Direction direction, Role role, const char* constant, std::vector<std::string> required,
                       std::string summary) {
    if (!has_protocol(direction, "srt") || !class_const(cls, "mode", constant, direction == Direction::Output)) {
      return;
    }
    std::vector<std::string> optional{"params", "local_host"};
    modes.push_back(make_mode("srt", direction, role, "ffmpeg-protocol", "srt", "mpegts", std::move(required),
                              std::move(optional), true, std::move(summary)));
  };
  for (const Direction direction : {Direction::Input, Direction::Output}) {
    add(direction, Role::Client, "caller", {"remote_host", "remote_port"},
        "SRT caller: з'єднання з віддаленим портом. Окремого локального порту ця збірка не має.");
    add(direction, Role::Listener, "listener", {"local_port"},
        "SRT listener: очікування на локальній адресі й порту.");
    add(direction, Role::Rendezvous, "rendezvous", {"remote_host", "remote_port", "local_port"},
        "SRT rendezvous: у URL один порт, тому локальний і віддалений порти мають збігатися.");
  }
}

void add_rtp(std::vector<Mode>& modes) {
  if (has_protocol(Direction::Input, "rtp") && library().demuxers.count("rtp") != 0) {
    modes.push_back(make_mode(
        "rtp", Direction::Input, Role::Listener, "ffmpeg-demuxer", "rtp", "rtp", {"local_port"},
        {"local_host", "rtcp_port", "params"}, true,
        "MPEG-TS over RTP: демуксер rtp знімає заголовок RTP (payload type 33). Окремий RTCP — rtcp_port."));
  }
  if (has_protocol(Direction::Output, "rtp") && library().muxers.count("rtp_mpegts") != 0) {
    modes.push_back(make_mode(
        "rtp", Direction::Output, Role::Client, "ffmpeg-muxer", "rtp_mpegts", "rtp_mpegts",
        {"remote_host", "remote_port"}, {"local_host", "local_port", "rtcp_port", "params"}, true,
        "MPEG-TS over RTP: муксер rtp_mpegts додає заголовки RTP (payload type 33). Протокол rtp:// сам їх не додає."));
  }
  if (has_protocol(Direction::Input, "rtp") && library().demuxers.count("sdp") != 0) {
    modes.push_back(make_mode("rtp", Direction::Input, Role::Client, "ffmpeg-demuxer", "sdp", "sdp", {"path"},
                              {"params"}, false,
                              "Елементарний RTP H.264/H.265 за файлом SDP. Це не MPEG-TS."));
  }
  if (has_protocol(Direction::Output, "rtp") && library().muxers.count("rtp") != 0) {
    modes.push_back(make_mode(
        "rtp", Direction::Output, Role::Client, "ffmpeg-muxer", "rtp", "rtp",
        {"remote_host", "remote_port", "sdp"}, {"local_host", "local_port", "rtcp_port", "params"}, true,
        "Елементарний RTP H.264/H.265. Після старту записується SDP; payload type береться з кодека."));
  }
}

void add_tcp(std::vector<Mode>& modes) {
  const AVClass* cls = protocol_class(library(), "tcp");
  for (const Direction direction : {Direction::Input, Direction::Output}) {
    if (!has_protocol(direction, "tcp")) {
      continue;
    }
    const bool output = direction == Direction::Output;
    modes.push_back(make_mode("tcp", direction, Role::Client, "ffmpeg-protocol", "tcp", "mpegts",
                              {"remote_host", "remote_port"}, {"params"}, true,
                              "TCP-клієнт: з'єднання з віддаленим портом."));
    if (cls != nullptr && class_option(cls, "listen", output)) {
      modes.push_back(make_mode("tcp", direction, Role::Listener, "ffmpeg-protocol", "tcp", "mpegts", {"local_port"},
                                {"local_host", "params"}, true,
                                direction == Direction::Input
                                    ? "TCP-слухач: локальна адреса й порт, вхідне з'єднання."
                                    : "TCP-слухач: локальна адреса й порт, вихід після підключення клієнта."));
    }
  }
}

void add_http_family(std::vector<Mode>& modes, const std::string& protocol) {
  const AVClass* cls = protocol_class(library(), protocol);
  for (const Direction direction : {Direction::Input, Direction::Output}) {
    if (!has_protocol(direction, protocol)) {
      continue;
    }
    const bool output = direction == Direction::Output;
    const Role client_role = Role::Client;
    modes.push_back(make_mode(
        protocol, direction, client_role, "ffmpeg-protocol", protocol, direction == Direction::Output ? "mpegts" : "",
        {"remote_host"}, {"remote_port", "path", "params"}, true,
        direction == Direction::Input ? "HTTP-клієнт читає URL. Порт у URL — порт віддаленого джерела."
                                      : "HTTP-клієнт надсилає потік на URL. Це не сервер."));
    if (cls != nullptr && class_option(cls, "listen", output)) {
      const Role role = direction == Direction::Output ? Role::Server : Role::Listener;
      modes.push_back(make_mode(protocol, direction, role, "ffmpeg-protocol", protocol,
                                direction == Direction::Output ? "mpegts" : "", {"local_port"},
                                {"local_host", "path", "params"}, true,
                                direction == Direction::Output
                                    ? "HTTP-сервер бібліотеки віддає MPEG-TS на локальній адресі й порту."
                                    : "HTTP-слухач приймає тіло запиту на локальній адресі й порту."));
    }
  }
}

void add_rtmp_family(std::vector<Mode>& modes, const std::string& protocol) {
  const AVClass* cls = protocol_class(library(), protocol);
  if (has_protocol(Direction::Input, protocol)) {
    modes.push_back(make_mode(protocol, Direction::Input, Role::Client, "ffmpeg-protocol", protocol, "",
                              {"remote_host", "app", "playpath"}, {"remote_port", "params"}, true,
                              "RTMP-клієнт. Віддалений порт за замовчуванням 1935."));
    if (cls != nullptr && (class_option(cls, "rtmp_listen", false) || class_option(cls, "listen", false))) {
      modes.push_back(make_mode(protocol, Direction::Input, Role::Listener, "ffmpeg-protocol", protocol, "",
                                {"local_port", "app", "playpath"}, {"local_host", "params"}, true,
                                "RTMP-слухач на локальній адресі й порту."));
    }
  }
  if (has_protocol(Direction::Output, protocol)) {
    modes.push_back(make_mode(protocol, Direction::Output, Role::Client, "ffmpeg-protocol", protocol, "flv",
                              {"remote_host", "app", "playpath"}, {"remote_port", "params"}, true,
                              "Публікація RTMP на сервер. Вихід не є RTMP-сервером."));
  }
}

void add_pull_client(std::vector<Mode>& modes, const std::string& protocol) {
  if (!has_protocol(Direction::Input, protocol)) {
    return;
  }
  modes.push_back(make_mode(protocol, Direction::Input, Role::Client, "ffmpeg-protocol", protocol, "", {"remote_host"},
                            {"remote_port", "path", "params"}, true,
                            "Клієнт протоколу. Порт — порт віддаленого джерела."));
}

void add_rtsp(std::vector<Mode>& modes) {
  if (library().demuxers.count("rtsp") != 0) {
    modes.push_back(make_mode("rtsp", Direction::Input, Role::Client, "ffmpeg-demuxer", "rtsp", "", {"remote_host"},
                              {"remote_port", "path", "rtp_port_min", "rtp_port_max", "params"}, true,
                              "RTSP-клієнт. RTP/RTCP-порти — локальний діапазон min/max, не один універсальний порт."));
    if (class_const(demuxer_class("rtsp"), "rtsp_flags", "listen", false)) {
      modes.push_back(make_mode("rtsp", Direction::Input, Role::Listener, "ffmpeg-demuxer", "rtsp", "", {"local_port"},
                                {"local_host", "path", "rtp_port_min", "rtp_port_max", "params"}, true,
                                "RTSP-слухач: локальна адреса й порт, плюс діапазон RTP/RTCP."));
    }
  }
  if (library().muxers.count("rtsp") != 0) {
    modes.push_back(make_mode("rtsp", Direction::Output, Role::Client, "ffmpeg-muxer", "rtsp", "rtsp", {"remote_host"},
                              {"remote_port", "path", "rtp_port_min", "rtp_port_max", "params"}, true,
                              "Публікація на RTSP-сервер. Це клієнт, не сервер."));
    if (class_const(muxer_class("rtsp"), "rtsp_flags", "listen", true)) {
      modes.push_back(make_mode("rtsp", Direction::Output, Role::Server, "ffmpeg-muxer", "rtsp", "rtsp", {"local_port"},
                                {"local_host", "path", "rtp_port_min", "rtp_port_max", "params"}, true,
                                "RTSP-сервер, якщо збірка FFmpeg позначає listen як параметр виходу."));
    }
  }
}

void add_file(std::vector<Mode>& modes) {
  if (has_protocol(Direction::Input, "file")) {
    modes.push_back(make_mode("file", Direction::Input, Role::Client, "ffmpeg-protocol", "file", "", {"path"}, {},
                              true, "Локальний файл. Портів немає."));
  }
  if (has_protocol(Direction::Output, "file")) {
    modes.push_back(make_mode("file", Direction::Output, Role::Client, "ffmpeg-protocol", "file", "", {"path"}, {},
                              false, "Запис у локальний файл. Портів немає."));
  }
}

void add_hls(std::vector<Mode>& modes) {
  if (library().demuxers.count("hls") != 0) {
    modes.push_back(make_mode("hls", Direction::Input, Role::Client, "ffmpeg-demuxer", "hls", "", {"remote_host"},
                              {"remote_port", "path", "params"}, true,
                              "Читання HLS як клієнт. Окремого HLS-сервера на вході немає."));
  }
  if (library().muxers.count("hls") != 0) {
    modes.push_back(make_mode(
        "hls", Direction::Output, Role::Server, "application", "hls", "hls", {"local_port"},
        {"local_host", "directory", "params"}, false,
        "HLS-сервер застосунку: FFmpeg пише плейлист, застосунок віддає його по HTTP. Затримка більша, ніж у MPEG-TS."));
  }
}

std::string json_escape(const std::string& text) {
  std::string out;
  for (const char ch : text) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += ch;
        break;
    }
  }
  return out;
}

std::string join_csv(const std::vector<std::string>& values) {
  std::ostringstream out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << values[i];
  }
  return out.str();
}

bool authorization_required(const std::string& protocol) {
  return protocol == "ftp" || protocol == "sftp" || protocol == "smb" || protocol == "libsmbclient" ||
         protocol == "icecast";
}

}  // namespace

std::vector<Mode> discover_modes() {
  std::vector<Mode> modes;
  add_file(modes);
  add_udp_like(modes, "udp");
  add_udp_like(modes, "udplite");
  add_srt(modes);
  add_rtp(modes);
  add_tcp(modes);
  add_http_family(modes, "http");
  add_http_family(modes, "https");
  add_rtmp_family(modes, "rtmp");
  add_rtmp_family(modes, "rtmps");
  add_rtmp_family(modes, "rtmpt");
  add_rtmp_family(modes, "rtmpts");
  add_pull_client(modes, "mmsh");
  add_pull_client(modes, "mmst");
  add_rtsp(modes);
  add_hls(modes);
  modes.erase(std::remove_if(modes.begin(), modes.end(),
                             [](const Mode& mode) { return authorization_required(mode.protocol); }),
              modes.end());
  std::sort(modes.begin(), modes.end(), [](const Mode& left, const Mode& right) {
    if (left.direction != right.direction) {
      return left.direction == Direction::Input;
    }
    if (left.protocol != right.protocol) {
      return left.protocol < right.protocol;
    }
    return to_string(left.role) < to_string(right.role);
  });
  return modes;
}

const OptionProbe& ffmpeg_probe() {
  static const FfmpegProbe probe;
  return probe;
}

std::string modes_text(const std::vector<Mode>& modes) {
  std::ostringstream out;
  out << "Доступні режими цієї збірки FFmpeg і застосунку\n";
  for (const Mode& mode : modes) {
    const char* direction = mode.direction == Direction::Input ? "вхід " : "вихід";
    out << direction << '\t' << mode.protocol << '\t' << to_string(mode.role) << '\t' << mode.provider << '\n';
    out << "  обов'язково: " << (mode.required.empty() ? "-" : join_csv(mode.required)) << '\n';
    out << "  опційно: " << (mode.optional.empty() ? "-" : join_csv(mode.optional)) << '\n';
    out << "  " << mode.summary << '\n';
  }
  return out.str();
}

std::string modes_json(const std::vector<Mode>& modes) {
  std::ostringstream out;
  out << "{\"modes\":[";
  for (std::size_t i = 0; i < modes.size(); ++i) {
    const Mode& mode = modes[i];
    if (i != 0) {
      out << ',';
    }
    out << "{\"direction\":\"" << to_string(mode.direction) << "\",\"protocol\":\"" << json_escape(mode.protocol)
        << "\",\"role\":\"" << to_string(mode.role) << "\",\"provider\":\"" << json_escape(mode.provider)
        << "\",\"container\":\"" << json_escape(mode.container) << "\",\"required\":[";
    for (std::size_t j = 0; j < mode.required.size(); ++j) {
      if (j != 0) {
        out << ',';
      }
      out << '"' << json_escape(mode.required[j]) << '"';
    }
    out << "],\"optional\":[";
    for (std::size_t j = 0; j < mode.optional.size(); ++j) {
      if (j != 0) {
        out << ',';
      }
      out << '"' << json_escape(mode.optional[j]) << '"';
    }
    out << "],\"summary\":\"" << json_escape(mode.summary) << "\"}";
  }
  out << "]}";
  return out.str();
}

bool ffmpeg_has_protocol_option(const std::string& protocol, const std::string& name, bool output) {
  return class_option(protocol_class(library(), protocol), name, output);
}

bool ffmpeg_has_format_const(bool muxer, const std::string& format, const std::string& unit,
                             const std::string& name, bool output) {
  const AVClass* cls = muxer ? muxer_class(format) : demuxer_class(format);
  return class_const(cls, unit, name, output);
}

}  // namespace rtx
