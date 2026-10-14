#include "color.hpp"
#include "check.hpp"

int main() {
  const rtx::ColorChoice hd = rtx::choose_color(AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 1920, 1080);
  CHECK(hd.matrix == rtx::Matrix::Bt709);
  CHECK(hd.range == rtx::Range::Limited);
  const rtx::ColorChoice sd = rtx::choose_color(AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, 640, 480);
  CHECK(sd.matrix == rtx::Matrix::Bt601);
  const rtx::ColorChoice tagged = rtx::choose_color(AVCOL_SPC_SMPTE170M, AVCOL_RANGE_MPEG, 1920, 1080);
  CHECK(tagged.matrix == rtx::Matrix::Bt601);
  const rtx::ColorChoice full = rtx::choose_color(AVCOL_SPC_BT709, AVCOL_RANGE_JPEG, 1280, 720);
  CHECK(full.range == rtx::Range::Full);

  const rtx::ColorCoeffs coeff = rtx::coeffs_for(hd);
  CHECK(coeff.y_r == 47);
  CHECK(coeff.y_bias == 16);
  int y = 0;
  int u = 0;
  int v = 0;
  rtx::rgb_to_yuv(coeff, 255, 0, 0, y, u, v);
  CHECK(y == 63);
  int r = 0;
  int g = 0;
  int b = 0;
  rtx::yuv_to_rgb(coeff, 126, 128, 128, r, g, b);
  CHECK(r == 128);
  CHECK(g == 128);
  CHECK(b == 128);
  CHECK(rtx::av_space(hd) == AVCOL_SPC_BT709);
  CHECK(rtx::av_range(full) == AVCOL_RANGE_JPEG);
  return rtx_test::result();
}
