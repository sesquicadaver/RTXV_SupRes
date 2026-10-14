#include "catalog.hpp"
#include "check.hpp"
#include "platform.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
}

namespace {

int bind_udp(std::intptr_t fd, sockaddr_in& address) {
#if defined(_WIN32)
  return ::bind(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&address), static_cast<int>(sizeof(address)));
#else
  return ::bind(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&address), sizeof(address));
#endif
}

int socket_name(std::intptr_t fd, sockaddr_in& address) {
#if defined(_WIN32)
  int length = sizeof(address);
  return ::getsockname(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&address), &length);
#else
  socklen_t length = sizeof(address);
  return ::getsockname(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&address), &length);
#endif
}

int free_udp_port() {
  rtx::socket_startup();
  const std::intptr_t fd = rtx::socket_open(AF_INET, SOCK_DGRAM, 0);
  if (!rtx::socket_ok(fd)) {
    return 0;
  }
  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind_udp(fd, address) != 0 || socket_name(fd, address) != 0) {
    rtx::socket_close(fd);
    return 0;
  }
  const int port = ntohs(address.sin_port);
  rtx::socket_close(fd);
  return port;
}

int interrupt_cb(void* opaque) {
  return static_cast<std::atomic<bool>*>(opaque)->load() ? 0 : 1;
}

bool rtp_ts_datagram(const std::vector<uint8_t>& packet) {
  if (packet.size() < 12u + 188u) {
    return false;
  }
  if ((packet[0] & 0xC0) != 0x80 || (packet[1] & 0x7F) != 33) {
    return false;
  }
  std::size_t header = 12u + static_cast<std::size_t>(packet[0] & 0x0F) * 4u;
  if ((packet[0] & 0x10) != 0) {
    if (packet.size() < header + 4u) {
      return false;
    }
    const std::size_t extension = (static_cast<std::size_t>(packet[header + 2]) << 8 | packet[header + 3]) * 4u;
    header += 4u + extension;
  }
  return header < packet.size() && packet[header] == 0x47;
}

void sniff(int port, std::vector<uint8_t>& packet, std::atomic<bool>& bound) {
  rtx::socket_startup();
  const std::intptr_t fd = rtx::socket_open(AF_INET, SOCK_DGRAM, 0);
  if (!rtx::socket_ok(fd)) {
    bound = true;
    return;
  }
  rtx::socket_reuse(fd);
  rtx::socket_timeouts(fd, 4000, 4000);
  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<uint16_t>(port));
  if (bind_udp(fd, address) != 0) {
    rtx::socket_close(fd);
    bound = true;
    return;
  }
  bound = true;
  std::vector<char> buffer(65535);
  while (packet.empty()) {
    const long received = rtx::socket_recvfrom(fd, buffer.data(), buffer.size());
    if (received <= 0) {
      break;
    }
    std::vector<uint8_t> candidate(buffer.begin(), buffer.begin() + received);
    if (rtp_ts_datagram(candidate)) {
      packet = std::move(candidate);
    }
  }
  rtx::socket_close(fd);
}

std::string send_mpegts_over_rtp(int port, const std::string& muxer, const std::atomic<bool>* running = nullptr) {
  const std::string url = "rtp://127.0.0.1:" + std::to_string(port);
  AVFormatContext* output = nullptr;
  if (avformat_alloc_output_context2(&output, nullptr, muxer.c_str(), url.c_str()) < 0 || output == nullptr) {
    return "немає муксера " + muxer;
  }
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
  if (codec == nullptr) {
    avformat_free_context(output);
    return "немає програмного кодера mpeg4";
  }
  AVCodecContext* encoder = avcodec_alloc_context3(codec);
  encoder->width = 320;
  encoder->height = 240;
  encoder->pix_fmt = AV_PIX_FMT_YUV420P;
  encoder->time_base = AVRational{1, 25};
  encoder->framerate = AVRational{25, 1};
  encoder->bit_rate = 400000;
  encoder->gop_size = 12;
  encoder->max_b_frames = 0;
  AVStream* stream = nullptr;
  AVFrame* frame = nullptr;
  std::string error;
  if (avcodec_open2(encoder, codec, nullptr) < 0) {
    error = "кодер mpeg4 не відкрився";
  } else {
    stream = avformat_new_stream(output, nullptr);
    if (stream == nullptr || avcodec_parameters_from_context(stream->codecpar, encoder) < 0) {
      error = "немає вихідного потоку";
    }
  }
  if (error.empty()) {
    stream->time_base = encoder->time_base;
    if (avio_open(&output->pb, url.c_str(), AVIO_FLAG_WRITE) < 0) {
      error = "не вдалося відкрити " + url;
    } else if (avformat_write_header(output, nullptr) < 0) {
      error = "заголовок муксера " + muxer;
    }
  }
  if (error.empty()) {
    frame = av_frame_alloc();
    frame->format = encoder->pix_fmt;
    frame->width = encoder->width;
    frame->height = encoder->height;
    if (av_frame_get_buffer(frame, 0) < 0) {
      error = "немає буфера кадру";
    }
  }
  const int frames = running == nullptr ? 8 : 80;
  for (int index = 0; index < frames && error.empty(); ++index) {
    if (running != nullptr && !running->load()) {
      break;
    }
    if (av_frame_make_writable(frame) < 0) {
      error = "кадр недоступний для запису";
      break;
    }
    std::memset(frame->data[0], 32, static_cast<std::size_t>(frame->linesize[0]) * static_cast<std::size_t>(frame->height));
    std::memset(frame->data[1], 128, static_cast<std::size_t>(frame->linesize[1]) * static_cast<std::size_t>(frame->height) / 2);
    std::memset(frame->data[2], 128, static_cast<std::size_t>(frame->linesize[2]) * static_cast<std::size_t>(frame->height) / 2);
    frame->pts = index;
    if (avcodec_send_frame(encoder, frame) < 0) {
      error = "кодер не прийняв кадр";
      break;
    }
    AVPacket* packet = av_packet_alloc();
    int received = 0;
    while ((received = avcodec_receive_packet(encoder, packet)) == 0) {
      av_packet_rescale_ts(packet, encoder->time_base, stream->time_base);
      packet->stream_index = stream->index;
      if (av_interleaved_write_frame(output, packet) < 0) {
        error = "запис " + muxer;
        break;
      }
      av_packet_unref(packet);
    }
    if (error.empty() && received != AVERROR(EAGAIN) && received != AVERROR_EOF && received != 0) {
      error = "кодер не віддав пакет";
    }
    av_packet_free(&packet);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  if (encoder != nullptr) {
    avcodec_send_frame(encoder, nullptr);
  }
  if (output != nullptr && output->pb != nullptr && error.empty()) {
    av_write_trailer(output);
  }
  if (output != nullptr && output->pb != nullptr) {
    avio_closep(&output->pb);
  }
  av_frame_free(&frame);
  avcodec_free_context(&encoder);
  avformat_free_context(output);
  return error;
}

bool read_with_rtp_demuxer(int port, std::atomic<bool>& alive) {
  const std::string url = "rtp://127.0.0.1:" + std::to_string(port);
  AVFormatContext* input = avformat_alloc_context();
  if (input == nullptr) {
    return false;
  }
  input->probesize = 8 * 1024;
  input->max_analyze_duration = 200000;
  input->interrupt_callback.callback = interrupt_cb;
  input->interrupt_callback.opaque = &alive;
  AVInputFormat* format = const_cast<AVInputFormat*>(av_find_input_format("rtp"));
  if (format == nullptr) {
    avformat_free_context(input);
    return false;
  }
  if (avformat_open_input(&input, url.c_str(), format, nullptr) < 0) {
    return false;
  }
  if (avformat_find_stream_info(input, nullptr) < 0) {
    avformat_close_input(&input);
    return false;
  }
  AVPacket* packet = av_packet_alloc();
  if (packet == nullptr) {
    avformat_close_input(&input);
    return false;
  }
  const int read = av_read_frame(input, packet);
  bool start_code = false;
  if (read >= 0 && packet->data != nullptr) {
    const int limit = packet->size < 64 ? packet->size : 64;
    for (int index = 0; index + 3 < limit; ++index) {
      if (packet->data[index] == 0 && packet->data[index + 1] == 0 && packet->data[index + 2] == 1) {
        start_code = true;
        break;
      }
    }
  }
  const bool ok = read >= 0 && packet->size > 0 && start_code && packet->stream_index >= 0 &&
                  input->streams[packet->stream_index]->codecpar->codec_id == AV_CODEC_ID_MPEG4;
  av_packet_free(&packet);
  avformat_close_input(&input);
  return ok;
}

}  // namespace

int main() {
  avformat_network_init();
  av_log_set_level(AV_LOG_FATAL);
  const auto modes = rtx::discover_modes();
  const rtx::Mode* output = rtx::find_mode(modes, rtx::Direction::Output, "rtp", "client", "rtp_mpegts");
  CHECK(output != nullptr);
  if (output == nullptr) {
    return rtx_test::result();
  }

  const int sniff_port = free_udp_port();
  CHECK(sniff_port > 0);
  if (sniff_port <= 0) {
    return rtx_test::result();
  }
  std::vector<uint8_t> packet;
  std::atomic<bool> bound{false};
  std::thread receiver(sniff, sniff_port, std::ref(packet), std::ref(bound));
  while (!bound.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const std::string sniffed_error = send_mpegts_over_rtp(sniff_port, output->container);
  receiver.join();
  if (!sniffed_error.empty()) {
    std::cerr << sniffed_error << '\n';
  }
  CHECK(sniffed_error.empty());
  CHECK(rtp_ts_datagram(packet));

  const int client_port = free_udp_port();
  CHECK(client_port > 0);
  if (client_port <= 0) {
    return rtx_test::result();
  }
  std::atomic<bool> alive{true};
  std::string client_error;
  std::thread sender([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    client_error = send_mpegts_over_rtp(client_port, output->container, &alive);
  });
  std::thread watchdog([&] {
    for (int tick = 0; tick < 80 && alive.load(); ++tick) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    alive = false;
  });
  const bool received = read_with_rtp_demuxer(client_port, alive);
  alive = false;
  sender.join();
  watchdog.join();
  if (!client_error.empty()) {
    std::cerr << client_error << '\n';
  }
  CHECK(client_error.empty());
  CHECK(received);
  return rtx_test::result();
}
