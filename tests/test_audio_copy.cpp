#include "audio_copy.hpp"
#include "check.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

int main() {
  CHECK(rtx::muxer_carries_audio("mpegts", AV_CODEC_ID_AAC));
  CHECK(rtx::muxer_carries_audio("mpegts", AV_CODEC_ID_MP2));
  CHECK(rtx::muxer_carries_audio("mp4", AV_CODEC_ID_AAC));
  CHECK(!rtx::muxer_carries_audio("flv", AV_CODEC_ID_AC3));
  CHECK(!rtx::muxer_carries_audio("h264", AV_CODEC_ID_AAC));
  CHECK(!rtx::muxer_carries_audio("", AV_CODEC_ID_AAC));
  CHECK(!rtx::muxer_carries_audio("mpegts", AV_CODEC_ID_NONE));
  CHECK(!rtx::muxer_carries_audio("mpegts", AV_CODEC_ID_H264));
  CHECK(!rtx::muxer_carries_audio("no-such-muxer", AV_CODEC_ID_AAC));
  return rtx_test::result();
}
