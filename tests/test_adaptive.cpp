#include "adaptive.hpp"

#include <cassert>

int main() {
  rtx::AdaptiveQuality quality(2, 1, 4, true);
  assert(quality.quality() == 2);
  assert(!quality.observe(20.0, 33.0));
  assert(quality.observe(40.0, 33.0));
  assert(quality.quality() == 1);
  assert(!quality.observe(50.0, 33.0));
  assert(quality.quality() == 1);
  for (int i = 0; i < 7; ++i) {
    assert(!quality.observe(5.0, 33.0));
  }
  assert(quality.observe(5.0, 33.0));
  assert(quality.quality() == 2);
  rtx::AdaptiveQuality locked(4, 4, 4, false);
  assert(!locked.observe(100.0, 10.0));
  assert(locked.quality() == 4);
  return 0;
}
