#pragma once

#include <string>

namespace rtx {

// Чи можна копіювати цей кодек в названий муксер. Пряма відмова муксера, муксер без аудіо і не-аудіокодек — ні.
bool muxer_carries_audio(const std::string& format_name, int codec_id);

}  // namespace rtx
