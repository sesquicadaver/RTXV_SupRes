#pragma once

#include <cstdint>

extern "C" {
#include <libavutil/rational.h>
}

namespace rtx {

// Перерахунок міток з time_base входу в time_base кодера.
// Перший кадр отримує 0. Далі крок — різниця вхідних міток.
// Відсутня мітка, крок назад або розрив понад 2 с дає один кадр вперед.
struct PtsMap {
  bool have_last = false;
  std::int64_t last_in = 0;
  std::int64_t last_out = -1;
};

std::int64_t map_pts(PtsMap& map, std::int64_t input_pts, AVRational input_tb, AVRational output_tb);

}  // namespace rtx
