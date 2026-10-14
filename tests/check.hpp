#pragma once

#include <iostream>

namespace rtx_test {

struct State {
  int failures = 0;
};

inline State& state() {
  static State value;
  return value;
}

inline void check(bool ok, const char* expr, const char* file, int line) {
  if (!ok) {
    std::cerr << file << ':' << line << " CHECK failed: " << expr << '\n';
    ++state().failures;
  }
}

inline int result() { return state().failures == 0 ? 0 : 1; }

}  // namespace rtx_test

#define CHECK(expr) ::rtx_test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
