#include "adaptive.hpp"
#include "check.hpp"

int main() {
  rtx::AdaptiveQuality quality(2, 1, 4, true);
  CHECK(quality.quality() == 2);
  CHECK(!quality.observe(20.0, 33.0));
  CHECK(!quality.observe(40.0, 33.0));
  CHECK(quality.quality() == 2);
  CHECK(quality.observe(40.0, 33.0));
  CHECK(quality.quality() == 1);
  CHECK(!quality.observe(50.0, 33.0));
  CHECK(quality.quality() == 1);
  for (int i = 0; i < 7; ++i) {
    CHECK(!quality.observe(5.0, 33.0));
  }
  CHECK(quality.observe(5.0, 33.0));
  CHECK(quality.quality() == 2);
  rtx::AdaptiveQuality locked(4, 4, 4, false);
  CHECK(!locked.observe(100.0, 10.0));
  CHECK(locked.quality() == 4);
  return rtx_test::result();
}
