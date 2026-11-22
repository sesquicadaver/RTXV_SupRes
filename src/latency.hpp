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

// Проба масштабу 3→2 до відкриття виходу (як AdaptiveQuality: два повільні).
inline constexpr int kScaleProbeFrames = 3;

inline bool scale_probe_should_reduce(int slow_count, int sample_count) {
  if (sample_count <= 0 || slow_count <= 0) {
    return false;
  }
  return sample_count == 1 ? slow_count >= 1 : slow_count >= 2;
}

// Поширені стелі NVENC (Turing+): H.264 ≤4096, HEVC ≤8192 на вісь.
inline int nvenc_max_edge(bool hevc) { return hevc ? 8192 : 4096; }

inline bool nvenc_resolution_ok(int width, int height, bool hevc) {
  const int limit = nvenc_max_edge(hevc);
  return width > 0 && height > 0 && (width % 2) == 0 && (height % 2) == 0 && width <= limit &&
         height <= limit;
}

// Успішний прогін бенчмарку: процес завершився нулем, подія позначена ok, є кадри після прогріву.
inline bool benchmark_run_valid(int exit_code, bool event_ok, std::uint64_t samples) {
  return exit_code == 0 && event_ok && samples > 0;
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
