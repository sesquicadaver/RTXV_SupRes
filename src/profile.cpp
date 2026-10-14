#include "profile.hpp"

#include "json_mini.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace rtx {
namespace {

bool secret_parameter(const std::string& key) {
  return key == "passphrase" || key == "password" || key == "secret" || key == "token";
}

std::string params_json(const std::vector<std::pair<std::string, std::string>>& params) {
  std::ostringstream out;
  out << '[';
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << '"' << json_escape(params[i].first + "=" + params[i].second) << '"';
  }
  out << ']';
  return out.str();
}

void read_params(const Json& json, std::vector<std::pair<std::string, std::string>>& params) {
  const Json* node = json.find("params");
  if (node == nullptr || node->kind != Json::Kind::Array) {
    return;
  }
  for (const Json& item : node->array) {
    const auto split = item.text.find('=');
    if (item.kind == Json::Kind::String && split != std::string::npos && split > 0) {
      params.emplace_back(item.text.substr(0, split), item.text.substr(split + 1));
    }
  }
}

std::string endpoint_json(const ProfileEndpoint& endpoint) {
  std::ostringstream out;
  out << "{\"protocol\":\"" << json_escape(endpoint.protocol) << "\",\"role\":\"" << json_escape(endpoint.role)
      << "\",\"url\":\"" << json_escape(endpoint.url) << "\",\"remote_host\":\"" << json_escape(endpoint.remote_host)
      << "\",\"remote_port\":" << endpoint.remote_port << ",\"local_host\":\"" << json_escape(endpoint.local_host)
      << "\",\"local_port\":" << endpoint.local_port << ",\"path\":\"" << json_escape(endpoint.path)
      << "\",\"payload\":\"" << json_escape(endpoint.payload) << "\",\"directory\":\""
      << json_escape(endpoint.directory) << "\",\"app\":\"" << json_escape(endpoint.app) << "\",\"playpath\":\""
      << json_escape(endpoint.playpath) << "\",\"sdp\":\"" << json_escape(endpoint.sdp)
      << "\",\"rtp_port_min\":" << endpoint.rtp_port_min << ",\"rtp_port_max\":" << endpoint.rtp_port_max
      << ",\"rtcp_port\":" << endpoint.rtcp_port << ",\"params\":" << params_json(endpoint.params)
      << ",\"credential_id\":\"" << json_escape(endpoint.credential_id) << "\"}";
  return out.str();
}

ProfileEndpoint endpoint_from_json(const Json& json) {
  ProfileEndpoint endpoint;
  endpoint.protocol = json.text_or("protocol", {});
  endpoint.role = json.text_or("role", {});
  endpoint.url = json.text_or("url", {});
  endpoint.remote_host = json.text_or("remote_host", {});
  endpoint.remote_port = json.int_or("remote_port", -1);
  endpoint.local_host = json.text_or("local_host", {});
  endpoint.local_port = json.int_or("local_port", -1);
  endpoint.path = json.text_or("path", {});
  endpoint.payload = json.text_or("payload", {});
  endpoint.directory = json.text_or("directory", {});
  endpoint.credential_id = json.text_or("credential_id", {});
  endpoint.app = json.text_or("app", {});
  endpoint.playpath = json.text_or("playpath", {});
  endpoint.sdp = json.text_or("sdp", {});
  endpoint.rtp_port_min = json.int_or("rtp_port_min", -1);
  endpoint.rtp_port_max = json.int_or("rtp_port_max", -1);
  endpoint.rtcp_port = json.int_or("rtcp_port", -1);
  read_params(json, endpoint.params);
  return endpoint;
}

void copy_endpoint(const ProfileEndpoint& profile, Endpoint& endpoint) {
  endpoint.protocol = profile.protocol;
  endpoint.role = profile.role;
  endpoint.url = profile.url;
  endpoint.remote_host = profile.remote_host;
  endpoint.remote_port = profile.remote_port;
  endpoint.local_host = profile.local_host;
  endpoint.local_port = profile.local_port;
  endpoint.path = profile.path;
  endpoint.payload = profile.payload;
  endpoint.directory = profile.directory;
  endpoint.app = profile.app;
  endpoint.playpath = profile.playpath;
  endpoint.sdp = profile.sdp;
  endpoint.rtp_port_min = profile.rtp_port_min;
  endpoint.rtp_port_max = profile.rtp_port_max;
  endpoint.rtcp_port = profile.rtcp_port;
  endpoint.params = profile.params;
}

ProfileEndpoint capture_endpoint(const Endpoint& endpoint) {
  ProfileEndpoint profile;
  profile.protocol = endpoint.protocol;
  profile.role = endpoint.role;
  profile.url = endpoint.url;
  profile.remote_host = endpoint.remote_host;
  profile.remote_port = endpoint.remote_port;
  profile.local_host = endpoint.local_host;
  profile.local_port = endpoint.local_port;
  profile.path = endpoint.path;
  profile.payload = endpoint.payload;
  profile.directory = endpoint.directory;
  profile.app = endpoint.app;
  profile.playpath = endpoint.playpath;
  profile.sdp = endpoint.sdp;
  profile.rtp_port_min = endpoint.rtp_port_min;
  profile.rtp_port_max = endpoint.rtp_port_max;
  profile.rtcp_port = endpoint.rtcp_port;
  profile.params = endpoint.params;
  return profile;
}

std::vector<std::pair<std::string, std::string>> take_secret_params(
    std::vector<std::pair<std::string, std::string>>& params) {
  std::vector<std::pair<std::string, std::string>> secrets;
  std::vector<std::pair<std::string, std::string>> kept;
  for (const auto& param : params) {
    if (secret_parameter(param.first)) {
      secrets.push_back(param);
    } else {
      kept.push_back(param);
    }
  }
  params = std::move(kept);
  return secrets;
}

std::string encode_secret_blob(const std::string& password,
                               const std::vector<std::pair<std::string, std::string>>& query,
                               const std::vector<std::pair<std::string, std::string>>& input_params,
                               const std::vector<std::pair<std::string, std::string>>& output_params) {
  if (query.empty() && input_params.empty() && output_params.empty()) {
    return password;
  }
  const auto object = [](const std::vector<std::pair<std::string, std::string>>& values) {
    std::ostringstream out;
    out << '{';
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i != 0) {
        out << ',';
      }
      out << '"' << json_escape(values[i].first) << "\":\"" << json_escape(values[i].second) << '"';
    }
    out << '}';
    return out.str();
  };
  return std::string("{\"password\":\"") + json_escape(password) + "\",\"query\":" + object(query) +
         ",\"input_params\":" + object(input_params) + ",\"output_params\":" + object(output_params) + "}";
}

void restore_param_object(const Json* node, std::vector<std::pair<std::string, std::string>>& params) {
  if (node == nullptr || node->kind != Json::Kind::Object) {
    return;
  }
  for (const auto& item : node->object) {
    if (item.second.kind == Json::Kind::String) {
      params.emplace_back(item.first, item.second.text);
    }
  }
}

std::string append_query(std::string url, const std::vector<std::pair<std::string, std::string>>& query) {
  for (const auto& item : query) {
    url += (url.find('?') == std::string::npos ? '?' : '&');
    url += item.first + "=" + item.second;
  }
  return url;
}

struct SecretParts {
  std::string password;
  std::vector<std::pair<std::string, std::string>> query;
  std::vector<std::pair<std::string, std::string>> input_params;
  std::vector<std::pair<std::string, std::string>> output_params;

  bool any() const { return !password.empty() || !query.empty() || !input_params.empty() || !output_params.empty(); }
};

bool decode_secret_blob(const std::string& blob, SecretParts& parts, std::string& error) {
  parts = {};
  if (blob.empty() || blob.front() != '{') {
    parts.password = blob;
    error.clear();
    return true;
  }
  Json json;
  if (!parse_json(blob, json, error)) {
    return false;
  }
  parts.password = json.text_or("password", {});
  restore_param_object(json.find("query"), parts.query);
  restore_param_object(json.find("input_params"), parts.input_params);
  restore_param_object(json.find("output_params"), parts.output_params);
  error.clear();
  return true;
}

void overlay_secret_params(std::vector<std::pair<std::string, std::string>>& base,
                           const std::vector<std::pair<std::string, std::string>>& incoming) {
  for (const auto& item : incoming) {
    base.erase(std::remove_if(base.begin(), base.end(),
                              [&](const std::pair<std::string, std::string>& kept) { return kept.first == item.first; }),
               base.end());
    if (!item.second.empty()) {
      base.push_back(item);
    }
  }
}

std::string sealed_url(const UrlSecret& parts) {
  return parts.user.empty() ? parts.url : join_url_secret(parts.url, parts.user, {});
}

bool atomic_write(const std::filesystem::path& path, const std::string& body, std::string& error) {
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) {
      error = "не вдалося записати тимчасовий профіль";
      return false;
    }
    file << body;
    file.flush();
    if (!file) {
      error = "запис профілю перервано";
      std::filesystem::remove(temporary);
      return false;
    }
  }
  std::error_code failure;
  std::filesystem::rename(temporary, path, failure);
  if (failure) {
    std::filesystem::remove(temporary);
    error = "не вдалося замінити файл профілю";
    return false;
  }
  error.clear();
  return true;
}

}  // namespace

UrlSecret split_url_secret(const std::string& url) {
  UrlSecret result;
  result.url = url;
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) {
    return result;
  }
  const std::size_t authority = scheme + 3;
  const auto boundary = url.find_first_of("/?", authority);
  const auto at = url.find('@', authority);
  if (at != std::string::npos && (boundary == std::string::npos || at < boundary)) {
    const std::string userinfo = url.substr(authority, at - authority);
    const auto colon = userinfo.find(':');
    if (colon == std::string::npos) {
      result.user = userinfo;
    } else {
      result.user = userinfo.substr(0, colon);
      result.password = userinfo.substr(colon + 1);
      result.has_password = true;
    }
    result.url = url.substr(0, authority) + url.substr(at + 1);
  }
  const auto query = result.url.find('?');
  if (query == std::string::npos) {
    return result;
  }
  const std::string base = result.url.substr(0, query);
  std::string kept;
  std::size_t start = query + 1;
  while (start <= result.url.size()) {
    const auto amp = result.url.find('&', start);
    const auto end = amp == std::string::npos ? result.url.size() : amp;
    const std::string piece = result.url.substr(start, end - start);
    const auto eq = piece.find('=');
    const std::string key = eq == std::string::npos ? piece : piece.substr(0, eq);
    const std::string value = eq == std::string::npos ? std::string{} : piece.substr(eq + 1);
    if (secret_parameter(key)) {
      result.query_secrets.emplace_back(key, value);
    } else if (!piece.empty()) {
      if (!kept.empty()) {
        kept += '&';
      }
      kept += piece;
    }
    if (amp == std::string::npos) {
      break;
    }
    start = amp + 1;
  }
  result.url = kept.empty() ? base : base + "?" + kept;
  return result;
}

std::string join_url_secret(const std::string& url, const std::string& user, const std::string& password) {
  if (user.empty() && password.empty()) {
    return url;
  }
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) {
    return url;
  }
  const std::string userinfo = password.empty() ? user : user + ":" + password;
  return url.substr(0, scheme + 3) + userinfo + "@" + url.substr(scheme + 3);
}

std::string profile_json(const Profile& profile) {
  std::ostringstream out;
  out << "{\"schema\":" << profile.schema << ",\"name\":\"" << json_escape(profile.name) << "\",\"gpu_uuid\":\""
      << json_escape(profile.gpu_uuid) << "\",\"scale\":" << profile.scale
      << ",\"quality_auto\":" << (profile.quality_auto ? "true" : "false") << ",\"quality\":" << profile.quality
      << ",\"bitrate\":" << profile.bitrate << ",\"codec\":\"" << json_escape(profile.codec)
      << "\",\"input\":" << endpoint_json(profile.input) << ",\"output\":" << endpoint_json(profile.output) << "}";
  return out.str();
}

bool parse_profile(const std::string& text, Profile& profile, std::string& error) {
  Json json;
  if (!parse_json(text, json, error)) {
    return false;
  }
  if (json.kind != Json::Kind::Object) {
    error = "профіль має бути об'єктом JSON";
    return false;
  }
  if (json.int_or("schema", 0) != kProfileSchema) {
    error = "непідтримувана версія схеми профілю";
    return false;
  }
  profile.schema = kProfileSchema;
  profile.name = json.text_or("name", {});
  profile.gpu_uuid = json.text_or("gpu_uuid", {});
  profile.scale = json.int_or("scale", 2);
  profile.quality_auto = json.bool_or("quality_auto", true);
  profile.quality = json.int_or("quality", 2);
  profile.bitrate = json.i64_or("bitrate", 0);
  profile.codec = json.text_or("codec", "h264");
  if (const Json* input = json.find("input")) {
    profile.input = endpoint_from_json(*input);
  }
  if (const Json* output = json.find("output")) {
    profile.output = endpoint_from_json(*output);
  }
  error.clear();
  return true;
}

bool save_profile_file(const std::filesystem::path& path, const Profile& profile, std::string& error) {
  if (path.has_parent_path()) {
    std::error_code failure;
    std::filesystem::create_directories(path.parent_path(), failure);
  }
  return atomic_write(path, profile_json(profile) + "\n", error);
}

bool load_profile_file(const std::filesystem::path& path, Profile& profile, std::string& error) {
  std::ifstream file(path);
  if (!file) {
    error = "профіль не відкрито";
    return false;
  }
  std::ostringstream body;
  body << file.rdbuf();
  return parse_profile(body.str(), profile, error);
}

bool seal_input_secret(Profile& profile, const std::string& raw_url, const std::string& secret_id, SecretStore& store,
                       std::string& error) {
  return commit_input_secret(profile, raw_url, secret_id, "", profile.input.credential_id, true, store, error);
}

bool commit_input_secret(Profile& profile, const std::string& raw_url, const std::string& secret_id,
                         const std::string& kept_url, const std::string& kept_credential, bool drop_owned,
                         SecretStore& store, std::string& error) {
  const std::string owned_credential = kept_credential;
  const auto input_secrets = take_secret_params(profile.input.params);
  const auto output_secrets = take_secret_params(profile.output.params);
  const UrlSecret parts = split_url_secret(raw_url);
  const std::string visible = sealed_url(parts);
  const bool same_address = !kept_url.empty() && visible == kept_url;

  SecretParts stored;
  if (!owned_credential.empty()) {
    std::string blob;
    if (!store.load(owned_credential, blob, error) || !decode_secret_blob(blob, stored, error)) {
      return false;
    }
  }

  SecretParts merged;
  if (parts.has_password) {
    merged.password = parts.password;
  } else if (same_address) {
    merged.password = stored.password;
  }
  if (same_address) {
    merged.query = stored.query;
    overlay_secret_params(merged.query, parts.query_secrets);
  } else {
    for (const auto& item : parts.query_secrets) {
      if (!item.second.empty()) {
        merged.query.push_back(item);
      }
    }
  }
  merged.input_params = stored.input_params;
  merged.output_params = stored.output_params;
  overlay_secret_params(merged.input_params, input_secrets);
  overlay_secret_params(merged.output_params, output_secrets);

  profile.input.url = visible;
  if (!merged.any()) {
    profile.input.credential_id.clear();
    if (drop_owned && !owned_credential.empty()) {
      store.remove(owned_credential, error);
    }
    error.clear();
    return true;
  }
  if (secret_id.empty()) {
    error = "немає ідентифікатора для сховища облікових даних";
    return false;
  }
  const std::string blob =
      encode_secret_blob(merged.password, merged.query, merged.input_params, merged.output_params);
  if (!store.save(secret_id, blob, error)) {
    return false;
  }
  profile.input.credential_id = secret_id;
  if (drop_owned && !owned_credential.empty() && owned_credential != secret_id) {
    store.remove(owned_credential, error);
  }
  error.clear();
  return true;
}

bool open_input_secret(Profile& profile, SecretStore& store, std::string& error) {
  if (profile.input.credential_id.empty()) {
    error.clear();
    return true;
  }
  std::string blob;
  if (!store.load(profile.input.credential_id, blob, error)) {
    return false;
  }
  SecretParts parts;
  if (!decode_secret_blob(blob, parts, error)) {
    return false;
  }
  const UrlSecret url = split_url_secret(profile.input.url);
  profile.input.url = append_query(join_url_secret(url.url, url.user, parts.password), parts.query);
  profile.input.params.insert(profile.input.params.end(), parts.input_params.begin(), parts.input_params.end());
  profile.output.params.insert(profile.output.params.end(), parts.output_params.begin(), parts.output_params.end());
  error.clear();
  return true;
}

void apply_profile(const Profile& profile, Settings& settings) {
  settings.gpu_uuid = profile.gpu_uuid;
  settings.scale = profile.scale;
  settings.quality_auto = profile.quality_auto;
  settings.quality = profile.quality;
  settings.bitrate = profile.bitrate;
  settings.codec = profile.codec;
  copy_endpoint(profile.input, settings.input);
  copy_endpoint(profile.output, settings.output);
}

Profile profile_from_settings(const Settings& settings, const std::string& name, const std::string& gpu_uuid) {
  Profile profile;
  profile.name = name;
  profile.gpu_uuid = gpu_uuid.empty() ? settings.gpu_uuid : gpu_uuid;
  profile.scale = settings.scale;
  profile.quality_auto = settings.quality_auto;
  profile.quality = settings.quality;
  profile.bitrate = settings.bitrate;
  profile.codec = settings.codec;
  profile.input = capture_endpoint(settings.input);
  profile.output = capture_endpoint(settings.output);
  return profile;
}

}  // namespace rtx
