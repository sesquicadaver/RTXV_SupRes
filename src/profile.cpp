#include "profile.hpp"

#include "json_mini.hpp"

#include <fstream>
#include <sstream>

namespace rtx {
namespace {

std::string endpoint_json(const ProfileEndpoint& endpoint) {
  std::ostringstream out;
  out << "{\"protocol\":\"" << json_escape(endpoint.protocol) << "\",\"role\":\"" << json_escape(endpoint.role)
      << "\",\"url\":\"" << json_escape(endpoint.url) << "\",\"remote_host\":\"" << json_escape(endpoint.remote_host)
      << "\",\"remote_port\":" << endpoint.remote_port << ",\"local_host\":\"" << json_escape(endpoint.local_host)
      << "\",\"local_port\":" << endpoint.local_port << ",\"path\":\"" << json_escape(endpoint.path)
      << "\",\"payload\":\"" << json_escape(endpoint.payload) << "\",\"directory\":\""
      << json_escape(endpoint.directory) << "\",\"credential_id\":\"" << json_escape(endpoint.credential_id) << "\"}";
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
  return profile;
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
  const auto slash = url.find('/', authority);
  const auto at = url.find('@', authority);
  if (at == std::string::npos || (slash != std::string::npos && at > slash)) {
    return result;
  }
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
  const UrlSecret parts = split_url_secret(raw_url);
  profile.input.url = parts.has_password ? join_url_secret(parts.url, parts.user, {}) : parts.url;
  if (!parts.has_password) {
    if (!profile.input.credential_id.empty()) {
      store.remove(profile.input.credential_id, error);
    }
    profile.input.credential_id.clear();
    error.clear();
    return true;
  }
  if (secret_id.empty()) {
    error = "немає ідентифікатора для сховища облікових даних";
    return false;
  }
  if (!store.save(secret_id, parts.password, error)) {
    return false;
  }
  profile.input.credential_id = secret_id;
  error.clear();
  return true;
}

bool open_input_secret(Profile& profile, SecretStore& store, std::string& error) {
  if (profile.input.credential_id.empty()) {
    error.clear();
    return true;
  }
  std::string password;
  if (!store.load(profile.input.credential_id, password, error)) {
    return false;
  }
  const UrlSecret parts = split_url_secret(profile.input.url);
  profile.input.url = join_url_secret(parts.url, parts.user, password);
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
