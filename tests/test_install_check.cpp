#include "check.hpp"
#include "install_check.hpp"

#include <filesystem>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

namespace {

bool write_one_frame(const std::string& path) {
  const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
  if (encoder == nullptr) {
    return false;
  }
  AVFormatContext* format = nullptr;
  if (avformat_alloc_output_context2(&format, nullptr, "mpegts", path.c_str()) < 0 || format == nullptr) {
    return false;
  }
  AVStream* stream = avformat_new_stream(format, nullptr);
  AVCodecContext* codec = avcodec_alloc_context3(encoder);
  if (stream == nullptr || codec == nullptr) {
    avcodec_free_context(&codec);
    avformat_free_context(format);
    return false;
  }
  codec->width = 64;
  codec->height = 64;
  codec->pix_fmt = AV_PIX_FMT_YUV420P;
  codec->time_base = AVRational{1, 25};
  codec->framerate = AVRational{25, 1};
  codec->bit_rate = 400000;
  if ((format->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
    codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  if (avcodec_open2(codec, encoder, nullptr) < 0 || avcodec_parameters_from_context(stream->codecpar, codec) < 0) {
    avcodec_free_context(&codec);
    avformat_free_context(format);
    return false;
  }
  stream->time_base = codec->time_base;
  if (avio_open(&format->pb, path.c_str(), AVIO_FLAG_WRITE) < 0 || avformat_write_header(format, nullptr) < 0) {
    avcodec_free_context(&codec);
    if (format->pb != nullptr) {
      avio_closep(&format->pb);
    }
    avformat_free_context(format);
    return false;
  }
  AVFrame* frame = av_frame_alloc();
  bool wrote = false;
  if (frame != nullptr) {
    frame->format = codec->pix_fmt;
    frame->width = codec->width;
    frame->height = codec->height;
    if (av_frame_get_buffer(frame, 0) == 0) {
      for (int y = 0; y < frame->height; ++y) {
        for (int x = 0; x < frame->width; ++x) {
          frame->data[0][y * frame->linesize[0] + x] = 16;
        }
      }
      for (int y = 0; y < frame->height / 2; ++y) {
        for (int x = 0; x < frame->width / 2; ++x) {
          frame->data[1][y * frame->linesize[1] + x] = 128;
          frame->data[2][y * frame->linesize[2] + x] = 128;
        }
      }
      wrote = avcodec_send_frame(codec, frame) == 0;
    }
  }
  AVPacket* packet = av_packet_alloc();
  if (wrote && packet != nullptr) {
    while (avcodec_receive_packet(codec, packet) == 0) {
      av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
      packet->stream_index = stream->index;
      wrote = av_interleaved_write_frame(format, packet) == 0;
      av_packet_unref(packet);
    }
    avcodec_send_frame(codec, nullptr);
    while (avcodec_receive_packet(codec, packet) == 0) {
      av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
      packet->stream_index = stream->index;
      wrote = av_interleaved_write_frame(format, packet) == 0 && wrote;
      av_packet_unref(packet);
    }
  }
  av_write_trailer(format);
  av_packet_free(&packet);
  av_frame_free(&frame);
  avcodec_free_context(&codec);
  avio_closep(&format->pb);
  avformat_free_context(format);
  return wrote;
}

}  // namespace

int main() {
  rtx::SampleView empty;
  CHECK(!rtx::install_sample_ok(0, empty));
  rtx::SampleView frame;
  frame.frames = 1;
  frame.width = 64;
  frame.height = 64;
  CHECK(rtx::install_sample_ok(0, frame));
  CHECK(!rtx::install_sample_ok(1, frame));
  frame.width = 0;
  CHECK(!rtx::install_sample_ok(0, frame));

  const std::filesystem::path path = std::filesystem::temp_directory_path() / "rtx-vsr-install-check-frame.ts";
  CHECK(write_one_frame(path.string()));
  std::string error;
  const rtx::SampleView counted = rtx::count_video_frames(path.string(), error);
  CHECK(error.empty());
  CHECK(counted.frames > 0);
  CHECK(counted.width == 64);
  CHECK(counted.height == 64);
  std::filesystem::remove(path);

  const rtx::SampleView missing = rtx::count_video_frames(path.string(), error);
  CHECK(missing.frames == 0);
  CHECK(!error.empty());
  return rtx_test::result();
}
