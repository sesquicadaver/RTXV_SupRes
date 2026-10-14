#include "json_mini.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>

namespace rtx {
namespace {

struct Parser {
  const std::string& text;
  std::size_t index = 0;
  std::string error;

  void skip() {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
      ++index;
    }
  }

  bool fail(const std::string& message) {
    error = message;
    return false;
  }

  bool parse_value(Json& value) {
    skip();
    if (index >= text.size()) {
      return fail("очікувалось значення JSON");
    }
    const char ch = text[index];
    if (ch == '{') {
      return parse_object(value);
    }
    if (ch == '[') {
      return parse_array(value);
    }
    if (ch == '"') {
      value.kind = Json::Kind::String;
      return parse_string(value.text);
    }
    if (ch == 't' || ch == 'f') {
      return parse_bool(value);
    }
    if (ch == 'n') {
      return parse_null(value);
    }
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      return parse_number(value);
    }
    return fail("невідомий токен JSON");
  }

  bool parse_object(Json& value) {
    value.kind = Json::Kind::Object;
    ++index;
    skip();
    if (index < text.size() && text[index] == '}') {
      ++index;
      return true;
    }
    while (index < text.size()) {
      skip();
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip();
      if (index >= text.size() || text[index] != ':') {
        return fail("очікувалась двокрапка");
      }
      ++index;
      Json child;
      if (!parse_value(child)) {
        return false;
      }
      value.object.emplace(std::move(key), std::move(child));
      skip();
      if (index < text.size() && text[index] == ',') {
        ++index;
        continue;
      }
      if (index < text.size() && text[index] == '}') {
        ++index;
        return true;
      }
      return fail("очікувалась кома або кінець об'єкта");
    }
    return fail("незакритий об'єкт");
  }

  bool parse_array(Json& value) {
    value.kind = Json::Kind::Array;
    ++index;
    skip();
    if (index < text.size() && text[index] == ']') {
      ++index;
      return true;
    }
    while (index < text.size()) {
      Json child;
      if (!parse_value(child)) {
        return false;
      }
      value.array.push_back(std::move(child));
      skip();
      if (index < text.size() && text[index] == ',') {
        ++index;
        continue;
      }
      if (index < text.size() && text[index] == ']') {
        ++index;
        return true;
      }
      return fail("очікувалась кома або кінець масиву");
    }
    return fail("незакритий масив");
  }

  bool parse_string(std::string& out) {
    if (index >= text.size() || text[index] != '"') {
      return fail("очікувався рядок");
    }
    ++index;
    out.clear();
    while (index < text.size()) {
      const char ch = text[index++];
      if (ch == '"') {
        return true;
      }
      if (ch != '\\') {
        out += ch;
        continue;
      }
      if (index >= text.size()) {
        return fail("обірване екранування");
      }
      const char escaped = text[index++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          out += escaped;
          break;
        case 'n':
          out += '\n';
          break;
        case 't':
          out += '\t';
          break;
        case 'r':
          out += '\r';
          break;
        default:
          return fail("невідоме екранування");
      }
    }
    return fail("незакритий рядок");
  }

  bool parse_bool(Json& value) {
    value.kind = Json::Kind::Bool;
    if (text.compare(index, 4, "true") == 0) {
      value.boolean = true;
      index += 4;
      return true;
    }
    if (text.compare(index, 5, "false") == 0) {
      value.boolean = false;
      index += 5;
      return true;
    }
    return fail("очікувався логічний літерал");
  }

  bool parse_null(Json& value) {
    if (text.compare(index, 4, "null") != 0) {
      return fail("очікувався null");
    }
    value.kind = Json::Kind::Null;
    index += 4;
    return true;
  }

  bool parse_number(Json& value) {
    const std::size_t start = index;
    if (text[index] == '-') {
      ++index;
    }
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
      ++index;
    }
    if (index < text.size() && text[index] == '.') {
      ++index;
      while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
      }
    }
    value.kind = Json::Kind::Number;
    try {
      value.number = std::stod(text.substr(start, index - start));
    } catch (const std::exception&) {
      return fail("некоректне число");
    }
    return true;
  }
};

}  // namespace

const Json* Json::find(const std::string& key) const {
  const auto it = object.find(key);
  if (it == object.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string Json::text_or(const std::string& key, const std::string& fallback) const {
  const Json* child = find(key);
  if (child == nullptr || child->kind != Kind::String) {
    return fallback;
  }
  return child->text;
}

int Json::int_or(const std::string& key, int fallback) const {
  const Json* child = find(key);
  if (child == nullptr || child->kind != Kind::Number) {
    return fallback;
  }
  return static_cast<int>(std::llround(child->number));
}

std::int64_t Json::i64_or(const std::string& key, std::int64_t fallback) const {
  const Json* child = find(key);
  if (child == nullptr || child->kind != Kind::Number) {
    return fallback;
  }
  return static_cast<std::int64_t>(std::llround(child->number));
}

bool Json::bool_or(const std::string& key, bool fallback) const {
  const Json* child = find(key);
  if (child == nullptr || child->kind != Kind::Bool) {
    return fallback;
  }
  return child->boolean;
}

std::string json_escape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const unsigned char ch : text) {
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
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (ch < 0x20) {
          break;
        }
        out += static_cast<char>(ch);
        break;
    }
  }
  return out;
}

bool parse_json(const std::string& text, Json& value, std::string& error) {
  Parser parser{text, 0, {}};
  if (!parser.parse_value(value)) {
    error = parser.error;
    return false;
  }
  parser.skip();
  if (parser.index != text.size()) {
    error = "зайві дані після JSON";
    return false;
  }
  error.clear();
  return true;
}

}  // namespace rtx
