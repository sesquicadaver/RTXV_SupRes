#pragma once

#include <string>

namespace rtx {

struct SampleView {
  int frames = 0;
  int width = 0;
  int height = 0;
};

/// Кадр після VSR. Вихід із кадром, але без підтверджених кадрів VSR або з переходом у bypass, не приймається.
bool install_sample_ok(int exit_code, const SampleView& view, std::uint64_t vsr_frames, std::uint64_t bypass_entries);

SampleView count_video_frames(const std::string& path, std::string& error);

/// Короткий тест файлу: декодування, VSR, кодування, перевірка кадру. Окремо для кожного GPU за UUID.
/// Це не бенчмарк затримки. Події сеансу на stdout вимкнені, лишається один JSON.
int run_install_check(const std::string& clip, const std::string& directory);

}  // namespace rtx
