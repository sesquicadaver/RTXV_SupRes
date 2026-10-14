#include "timestamps.hpp"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

namespace rtx {

std::int64_t map_pts(PtsMap& map, std::int64_t input_pts, AVRational input_tb, AVRational output_tb) {
  const auto advance = [&map]() {
    map.last_out += 1;
    return map.last_out;
  };
  if (!map.have_last) {
    map.have_last = true;
    map.last_out = 0;
    map.last_in = input_pts;
    return 0;
  }
  if (input_pts == AV_NOPTS_VALUE || map.last_in == AV_NOPTS_VALUE) {
    map.last_in = input_pts;
    return advance();
  }
  const std::int64_t delta = input_pts - map.last_in;
  const std::int64_t two_seconds = av_rescale_q(2, AVRational{1, 1}, input_tb);
  if (delta <= 0 || (two_seconds > 0 && delta > two_seconds)) {
    map.last_in = input_pts;
    return advance();
  }
  std::int64_t step = av_rescale_q(delta, input_tb, output_tb);
  if (step < 1) {
    step = 1;
  }
  map.last_in = input_pts;
  map.last_out += step;
  return map.last_out;
}

}  // namespace rtx
