#pragma once

#include <cstdint>
#include <string>

namespace rtx {

// Чи можна копіювати цей кодек в названий муксер. Пряма відмова муксера, муксер без аудіо і не-аудіокодек — ні.
bool muxer_carries_audio(const std::string& format_name, int codec_id);

// Бюджет черги аудіо до відкриття/запису виходу: ліміт пакетів і вікно PTS.
inline constexpr int kAudioQueueMaxPackets = 64;
inline constexpr double kAudioQueueMaxSeconds = 0.5;

// true — packet_pts застарів відносно newest_pts більше ніж keep_seconds (за time_base).
bool audio_pts_outside_budget(std::int64_t packet_pts, std::int64_t newest_pts, double time_base_seconds,
                              double keep_seconds = kAudioQueueMaxSeconds);

}  // namespace rtx
