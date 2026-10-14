#include "adaptive.hpp"

namespace rtx {

AdaptiveQuality::AdaptiveQuality(int initial, int min_quality, int max_quality, bool automatic)
    : quality_(initial), min_(min_quality), max_(max_quality), automatic_(automatic) {
  if (min_ > max_) {
    min_ = max_;
  }
  if (quality_ < min_) {
    quality_ = min_;
  }
  if (quality_ > max_) {
    quality_ = max_;
  }
}

bool AdaptiveQuality::observe(double process_ms, double budget_ms) {
  if (!automatic_ || budget_ms <= 0) {
    slow_streak_ = 0;
    fast_streak_ = 0;
    return false;
  }
  const double slow = budget_ms * 0.85;
  const double fast = budget_ms * 0.45;
  if (process_ms > slow) {
    fast_streak_ = 0;
    ++slow_streak_;
    if (slow_streak_ >= 2 && quality_ > min_) {
      --quality_;
      slow_streak_ = 0;
      return true;
    }
    return false;
  }
  if (process_ms < fast) {
    slow_streak_ = 0;
    ++fast_streak_;
    if (fast_streak_ >= 8 && quality_ < max_) {
      ++quality_;
      fast_streak_ = 0;
      return true;
    }
    return false;
  }
  slow_streak_ = 0;
  fast_streak_ = 0;
  return false;
}

}  // namespace rtx
