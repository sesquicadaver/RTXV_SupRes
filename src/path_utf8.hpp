#pragma once

#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rtx {

// Рядки профілю й GUI тримаємо в UTF-8. На Windows fs::path(std::string) читає ACP.
inline std::filesystem::path path_from_utf8(const std::string& text) {
#if defined(_WIN32)
  if (text.empty()) {
    return {};
  }
  const int needed =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) {
    return std::filesystem::path(text);
  }
  std::wstring wide(static_cast<std::size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), needed);
  return std::filesystem::path(wide);
#else
  return std::filesystem::path(text);
#endif
}

inline std::string utf8_from_path(const std::filesystem::path& path) {
#if defined(_WIN32)
  const std::wstring wide = path.wstring();
  if (wide.empty()) {
    return {};
  }
  const int needed =
      WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return path.string();
  }
  std::string utf8(static_cast<std::size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), needed, nullptr, nullptr);
  return utf8;
#else
  return path.string();
#endif
}

}  // namespace rtx
