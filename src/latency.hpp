#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace rtx {

// Очікування в черзі після того, як кадр уже готовий до обробки.
// ready_at стоїть після штучного --pace; decoded_at — момент виходу з NVDEC.
inline double proxy_queue_ms(std::chrono::steady_clock::time_point decoded_at,
                             std::chrono::steady_clock::time_point ready_at,
                             std::chrono::steady_clock::time_point taken) {
  const auto from = ready_at >= decoded_at ? ready_at : decoded_at;
  if (taken <= from) {
    return 0;
  }
  return std::chrono::duration<double, std::milli>(taken - from).count();
}

// Нуль зразків після прогріву не є вкладенням у бюджет: p95 тоді теж нуль.
inline bool measurement_within_budget(std::uint64_t samples, double p95_ms, double budget_ms) {
  return samples > 0 && p95_ms <= budget_ms;
}

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
