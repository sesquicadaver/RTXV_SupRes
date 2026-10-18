#include "audio_copy.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace rtx {

bool muxer_carries_audio(const std::string& format_name, int codec_id) {
  if (format_name.empty() || codec_id == AV_CODEC_ID_NONE) {
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

}  // namespace rtx
