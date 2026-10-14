#pragma once

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
  bool has_userinfo = false;
  bool has_password = false;
  std::vector<std::pair<std::string, std::string>> query_secrets;

  bool has_secret() const { return has_userinfo || has_password || !query_secrets.empty(); }
};

UrlSecret split_url_secret(const std::string& url);

std::string profile_json(const Profile& profile);
bool parse_profile(const std::string& text, Profile& profile, std::string& error);
bool save_profile_file(const std::filesystem::path& path, const Profile& profile, std::string& error);
bool load_profile_file(const std::filesystem::path& path, Profile& profile, std::string& error);

// Порожній рядок означає адресу без імені користувача і пароля. Інакше джерело відхиляється.
std::string password_access_error(const std::string& url,
                                 const std::vector<std::pair<std::string, std::string>>& params,
                                 const std::string& credential_id);
std::string password_access_error(const Profile& profile);

void apply_profile(const Profile& profile, Settings& settings);
Profile profile_from_settings(const Settings& settings, const std::string& name, const std::string& gpu_uuid);

}  // namespace rtx
