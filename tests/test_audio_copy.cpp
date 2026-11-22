#include "audio_copy.hpp"
#include "check.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

int main() {
  CHECK(rtx::muxer_carries_audio("mpegts", AV_CODEC_ID_AAC));
  CHECK(rtx::muxer_carries_audio("mpegts", AV_CODEC_ID_MP2));
  CHECK(rtx::muxer_carries_audio("mp4", AV_CODEC_ID_AAC));
  CHECK(rtx::muxer_carries_audio("matroska", AV_CODEC_ID_AAC));
  CHECK(!rtx::muxer_carries_audio("h264", AV_CODEC_ID_AAC));
  CHECK(!rtx::muxer_carries_audio("hevc", AV_CODEC_ID_AAC));
  CHECK(!rtx::muxer_carries_audio("rtp", AV_CODEC_ID_AAC));
  CHECK(!rtx::muxer_carries_audio("", AV_CODEC_ID_AAC));
  CHECK(!rtx::muxer_carries_audio("mpegts", AV_CODEC_ID_NONE));
  CHECK(!rtx::muxer_carries_audio("mpegts", AV_CODEC_ID_H264));
  CHECK(!rtx::muxer_carries_audio("no-such-muxer", AV_CODEC_ID_AAC));
  // rtp_mpegts — MPEG-TS у RTP; аудіо в TS можливе, якщо муксер FFmpeg його приймає.
  const bool rtp_mpegts = rtx::muxer_carries_audio("rtp_mpegts", AV_CODEC_ID_AAC);
  (void)rtp_mpegts;
  CHECK(rtx::kAudioQueueMaxPackets == 64);
  CHECK(!rtx::audio_pts_outside_budget(AV_NOPTS_VALUE, 1000, 1.0 / 48000.0));
  CHECK(!rtx::audio_pts_outside_budget(0, 1000, 1.0 / 48000.0, 0.5));
  CHECK(rtx::audio_pts_outside_budget(0, 48000, 1.0 / 48000.0, 0.5));
  CHECK(!rtx::audio_pts_outside_budget(2000, 1000, 1.0 / 48000.0, 0.5));
  return rtx_test::result();
}
