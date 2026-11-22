#include "audio_copy.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace rtx {

bool muxer_carries_audio(const std::string& format_name, int codec_id) {
  if (format_name.empty() || codec_id == AV_CODEC_ID_NONE) {
    return false;
  }
  // Елементарний RTP несе один ES; окремий аудіопотік сюди не вміщується.
  if (format_name == "rtp") {
    return false;
  }
  const AVOutputFormat* format = av_guess_format(format_name.c_str(), nullptr, nullptr);
  if (format == nullptr || format->audio_codec == AV_CODEC_ID_NONE) {
    return false;
  }
  const AVCodecID id = static_cast<AVCodecID>(codec_id);
  if (avcodec_get_type(id) != AVMEDIA_TYPE_AUDIO) {
    return false;
  }
  // 0 — муксер прямо відмовив. 1 — підтвердив. Від'ємне — немає явної відповіді (MPEG-TS і AAC).
  return avformat_query_codec(format, id, FF_COMPLIANCE_NORMAL) != 0;
}

bool audio_pts_outside_budget(std::int64_t packet_pts, std::int64_t newest_pts, double time_base_seconds,
                              double keep_seconds) {
  if (packet_pts == AV_NOPTS_VALUE || newest_pts == AV_NOPTS_VALUE || time_base_seconds <= 0 || keep_seconds <= 0) {
    return false;
  }
  if (newest_pts < packet_pts) {
    return false;
  }
  const double age_seconds = static_cast<double>(newest_pts - packet_pts) * time_base_seconds;
  return age_seconds > keep_seconds;
}

}  // namespace rtx
