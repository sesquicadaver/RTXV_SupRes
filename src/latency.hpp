#pragma once

#include <cstddef>
#include <cstdint>

namespace rtx {

// Час обробки проксі по кожному кадру. Кільце фіксованого розміру тримає піки
// останнього вікна, не згладжене середнє.
class ProxyLatency {
 public:
  static constexpr std::size_t kCapacity = 16384;

  void add(double milliseconds);
  void clear();
  std::uint64_t samples() const;
  double percentile(double fraction) const;

 private:
  double values_[kCapacity] = {};
  std::size_t count_ = 0;
  std::size_t next_ = 0;
};

}  // namespace rtx
