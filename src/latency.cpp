#include "latency.hpp"

#include <algorithm>
#include <vector>

namespace rtx {

void ProxyLatency::add(double milliseconds) {
  values_[next_] = milliseconds;
  next_ = (next_ + 1) % kCapacity;
  if (count_ < kCapacity) {
    ++count_;
  }
}

void ProxyLatency::clear() {
  count_ = 0;
  next_ = 0;
}

std::uint64_t ProxyLatency::samples() const { return count_; }

double ProxyLatency::percentile(double fraction) const {
  if (count_ == 0) {
    return 0;
  }
  std::vector<double> ordered;
  ordered.reserve(count_);
  if (count_ < kCapacity) {
    ordered.assign(values_, values_ + count_);
  } else {
    ordered.assign(values_ + next_, values_ + kCapacity);
    ordered.insert(ordered.end(), values_, values_ + next_);
  }
  const double clamped = std::min(1.0, std::max(0.0, fraction));
  std::size_t index = static_cast<std::size_t>(clamped * static_cast<double>(ordered.size() - 1));
  if (index >= ordered.size()) {
    index = ordered.size() - 1;
  }
  std::nth_element(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(index), ordered.end());
  return ordered[index];
}

}  // namespace rtx
