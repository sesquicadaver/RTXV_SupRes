#include "timestamps.hpp"
#include "check.hpp"

extern "C" {
#include <libavutil/avutil.h>
}

int main() {
  const AVRational input{1, 90000};
  const AVRational output{1, 30};
  rtx::PtsMap map;
  CHECK(rtx::map_pts(map, 100000, input, output) == 0);
  CHECK(rtx::map_pts(map, 103000, input, output) == 1);
  CHECK(rtx::map_pts(map, 106000, input, output) == 2);
  CHECK(rtx::map_pts(map, AV_NOPTS_VALUE, input, output) == 3);
  CHECK(rtx::map_pts(map, 1000, input, output) == 4);

  rtx::PtsMap gap;
  CHECK(rtx::map_pts(gap, 0, input, output) == 0);
  CHECK(rtx::map_pts(gap, 270000, input, output) == 1);

  const AVRational ntsc_out{1001, 30000};
  rtx::PtsMap ntsc;
  CHECK(rtx::map_pts(ntsc, 0, input, ntsc_out) == 0);
  CHECK(rtx::map_pts(ntsc, 3003, input, ntsc_out) == 1);
  return rtx_test::result();
}
