#pragma once

extern "C" {
#include <libavutil/pixfmt.h>
}

#if defined(__CUDACC__)
#define RTX_HD __host__ __device__
#else
#define RTX_HD
#endif

namespace rtx {

enum class Matrix { Bt601, Bt709, Bt2020 };
enum class Range { Limited, Full };

struct ColorChoice {
  Matrix matrix = Matrix::Bt709;
  Range range = Range::Limited;
};

// Цілі коефіцієнти ×256. y_bias — 16 для limited і 0 для full.
struct ColorCoeffs {
  int y_r = 47;
  int y_g = 157;
  int y_b = 16;
  int u_r = -26;
  int u_g = -86;
  int u_b = 112;
  int v_r = 112;
  int v_g = -102;
  int v_b = -10;
  int r_e = 459;
  int g_d = 55;
  int g_e = 136;
  int b_d = 541;
  int c_mul = 298;
  int y_bias = 16;
};

ColorChoice choose_color(int space, int range, int width, int height);
ColorCoeffs coeffs_for(ColorChoice choice);
int av_space(ColorChoice choice);
int av_range(ColorChoice choice);
int av_primaries(ColorChoice choice);
int av_trc(ColorChoice choice);

RTX_HD inline int clamp_u8(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return value;
}

RTX_HD inline int round_div256(int value) {
  if (value >= 0) {
    return (value + 128) >> 8;
  }
  return -(((-value) + 128) >> 8);
}

RTX_HD inline void yuv_to_rgb(const ColorCoeffs& coeff, int y, int u, int v, int& r, int& g, int& b) {
  const int c = y - coeff.y_bias;
  const int d = u - 128;
  const int e = v - 128;
  r = clamp_u8(round_div256(coeff.c_mul * c + coeff.r_e * e));
  g = clamp_u8(round_div256(coeff.c_mul * c - coeff.g_d * d - coeff.g_e * e));
  b = clamp_u8(round_div256(coeff.c_mul * c + coeff.b_d * d));
}

RTX_HD inline void rgb_to_yuv(const ColorCoeffs& coeff, int r, int g, int b, int& y, int& u, int& v) {
  y = clamp_u8(round_div256(coeff.y_r * r + coeff.y_g * g + coeff.y_b * b) + coeff.y_bias);
  u = clamp_u8(round_div256(coeff.u_r * r + coeff.u_g * g + coeff.u_b * b) + 128);
  v = clamp_u8(round_div256(coeff.v_r * r + coeff.v_g * g + coeff.v_b * b) + 128);
}

}  // namespace rtx
