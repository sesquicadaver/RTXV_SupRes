#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rtx {

struct Json {
  enum class Kind { Null, Bool, Number, String, Array, Object };

  Kind kind = Kind::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<Json> array;
  std::map<std::string, Json> object;

  const Json* find(const std::string& key) const;
  std::string text_or(const std::string& key, const std::string& fallback) const;
  int int_or(const std::string& key, int fallback) const;
  std::int64_t i64_or(const std::string& key, std::int64_t fallback) const;
  bool bool_or(const std::string& key, bool fallback) const;
};

std::string json_escape(const std::string& text);
bool parse_json(const std::string& text, Json& value, std::string& error);

}  // namespace rtx
