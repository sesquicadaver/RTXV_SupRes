#pragma once

#include "secrets.hpp"
#include "types.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace rtx {

inline constexpr int kProfileSchema = 1;

struct ProfileEndpoint {
  std::string protocol;
  std::string role;
  std::string url;
  std::string remote_host;
  int remote_port = -1;
  std::string local_host;
  int local_port = -1;
  std::string path;
  std::string payload;
  std::string directory;
  std::string credential_id;
  std::string app;
  std::string playpath;
  std::string sdp;
  int rtp_port_min = -1;
  int rtp_port_max = -1;
  int rtcp_port = -1;
  std::vector<std::pair<std::string, std::string>> params;
};

struct Profile {
  int schema = kProfileSchema;
  std::string name;
  std::string gpu_uuid;
  int scale = 2;
  bool quality_auto = true;
  int quality = 2;
  std::int64_t bitrate = 0;
  std::string codec = "h264";
  ProfileEndpoint input;
  ProfileEndpoint output;
};

struct UrlSecret {
  std::string url;
  std::string user;
  std::string password;
  bool has_password = false;
  std::vector<std::pair<std::string, std::string>> query_secrets;

  bool has_secret() const { return has_password || !query_secrets.empty(); }
};

UrlSecret split_url_secret(const std::string& url);
std::string join_url_secret(const std::string& url, const std::string& user, const std::string& password);

std::string profile_json(const Profile& profile);
bool parse_profile(const std::string& text, Profile& profile, std::string& error);
bool save_profile_file(const std::filesystem::path& path, const Profile& profile, std::string& error);
bool load_profile_file(const std::filesystem::path& path, Profile& profile, std::string& error);

// Пароль і секретні параметри URL ідуть у SecretStore. У JSON лишається адреса без них.
bool seal_input_secret(Profile& profile, const std::string& raw_url, const std::string& secret_id, SecretStore& store,
                       std::string& error);
// Новий secret_id отримує власну копію секрету, а не спільний kept_credential.
// Порожній kept_url замінює секрети URL вмістом raw_url. Інакше незмінна адреса зберігає
// пароль і секретні параметри зі сховища; ключ із порожнім значенням видаляє саме його.
// drop_owned видаляє kept_credential лише коли перезаписується профіль, який ним володіє.
bool commit_input_secret(Profile& profile, const std::string& raw_url, const std::string& secret_id,
                         const std::string& kept_url, const std::string& kept_credential, bool drop_owned,
                         SecretStore& store, std::string& error);
bool open_input_secret(Profile& profile, SecretStore& store, std::string& error);

void apply_profile(const Profile& profile, Settings& settings);
Profile profile_from_settings(const Settings& settings, const std::string& name, const std::string& gpu_uuid);

}  // namespace rtx
