#pragma once

#include <string>

namespace rtx {

struct SampleView {
  int frames = 0;
  int width = 0;
  int height = 0;
};

/// Кадр після декодування виходу. Нуль кадрів або порожній розмір не підтверджує встановлення.
bool install_sample_ok(int exit_code, const SampleView& view);

SampleView count_video_frames(const std::string& path, std::string& error);

/// Короткий тест файлу: декодування, VSR, кодування, перевірка кадру. Окремо для кожного GPU за UUID.
/// Це не бенчмарк затримки. Події сеансу на stdout вимкнені, лишається один JSON.
int run_install_check(const std::string& clip, const std::string& directory);

}  // namespace rtx
