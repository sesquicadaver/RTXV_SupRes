#pragma once

namespace rtx {

// Адаптація якості RTX VSR. process_ms — повний час кадру
// (декод, очікування, VSR, кодер, запис), budget_ms — інтервал кадру.
// Два повільні кадри знижують рівень, вісім швидких підвищують.
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
