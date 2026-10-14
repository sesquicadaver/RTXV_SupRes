#pragma once

namespace rtx {

// Адаптація інтенсивності RTX VSR. Роздільність не змінюється:
// інакше довелося б перестворювати кодер посеред потоку.
class AdaptiveQuality {
 public:
  AdaptiveQuality(int initial, int min_quality, int max_quality, bool automatic);

  int quality() const { return quality_; }
  bool automatic() const { return automatic_; }

  // Повертає true, якщо рівень змінено. process_ms і budget_ms — мілісекунди.
  bool observe(double process_ms, double budget_ms);

 private:
  int quality_ = 2;
  int min_ = 1;
  int max_ = 4;
  bool automatic_ = true;
  int slow_streak_ = 0;
  int fast_streak_ = 0;
};

}  // namespace rtx
