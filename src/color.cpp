#include "color.hpp"

namespace rtx {
namespace {

ColorCoeffs make_coeffs(int y_r, int y_g, int y_b, int u_r, int u_g, int u_b, int v_r, int v_g, int v_b, int r_e,
                        int g_d, int g_e, int b_d, int c_mul, int y_bias) {
  ColorCoeffs coeff;
  coeff.y_r = y_r;
  coeff.y_g = y_g;
  coeff.y_b = y_b;
  coeff.u_r = u_r;
  coeff.u_g = u_g;
  coeff.u_b = u_b;
  coeff.v_r = v_r;
  coeff.v_g = v_g;
  coeff.v_b = v_b;
  coeff.r_e = r_e;
  coeff.g_d = g_d;
  coeff.g_e = g_e;
  coeff.b_d = b_d;
  coeff.c_mul = c_mul;
  coeff.y_bias = y_bias;
  return coeff;
}

}  // namespace

ColorChoice choose_color(int space, int range, int width, int height) {
  ColorChoice choice;
  choice.range = range == AVCOL_RANGE_JPEG ? Range::Full : Range::Limited;
  switch (space) {
    case AVCOL_SPC_BT709:
      choice.matrix = Matrix::Bt709;
      break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
      choice.matrix = Matrix::Bt601;
      break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      choice.matrix = Matrix::Bt2020;
      break;
    default:
      choice.matrix = (width >= 1280 || height >= 720) ? Matrix::Bt709 : Matrix::Bt601;
      break;
  }
  return choice;
}

ColorCoeffs coeffs_for(ColorChoice choice) {
  const bool full = choice.range == Range::Full;
  if (choice.matrix == Matrix::Bt601) {
    return full ? make_coeffs(77, 150, 29, -43, -85, 128, 128, -107, -21, 359, 88, 183, 454, 256, 0)
                : make_coeffs(66, 129, 25, -38, -74, 112, 112, -94, -18, 409, 100, 208, 516, 298, 16);
  }
  if (choice.matrix == Matrix::Bt2020) {
    return full ? make_coeffs(67, 174, 15, -37, -92, 128, 128, -112, -16, 428, 48, 167, 548, 256, 0)
                : make_coeffs(58, 149, 13, -31, -81, 112, 112, -99, -13, 430, 48, 167, 548, 298, 16);
  }
  return full ? make_coeffs(54, 183, 19, -29, -99, 128, 128, -116, -12, 403, 48, 120, 475, 256, 0)
              : make_coeffs(47, 157, 16, -26, -86, 112, 112, -102, -10, 459, 55, 136, 541, 298, 16);
}

int av_space(ColorChoice choice) {
  if (choice.matrix == Matrix::Bt601) {
    return AVCOL_SPC_SMPTE170M;
  }
  if (choice.matrix == Matrix::Bt2020) {
    return AVCOL_SPC_BT2020_NCL;
  }
  return AVCOL_SPC_BT709;
}

int av_range(ColorChoice choice) { return choice.range == Range::Full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG; }

int av_primaries(ColorChoice choice) {
  if (choice.matrix == Matrix::Bt601) {
    return AVCOL_PRI_SMPTE170M;
  }
  if (choice.matrix == Matrix::Bt2020) {
    return AVCOL_PRI_BT2020;
  }
  return AVCOL_PRI_BT709;
}

int av_trc(ColorChoice choice) {
  if (choice.matrix == Matrix::Bt601) {
    return AVCOL_TRC_SMPTE170M;
  }
  if (choice.matrix == Matrix::Bt2020) {
    return AVCOL_TRC_BT2020_10;
  }
  return AVCOL_TRC_BT709;
}

}  // namespace rtx
