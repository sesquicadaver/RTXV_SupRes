#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace rtx {

// Один слот. Новий елемент замінює ще не забраний і збільшує лічильник відкидань.
// Черга не росте.
template <typename T>
class LatestSlot {
 public:
  using Deleter = void (*)(T*);

  explicit LatestSlot(Deleter deleter) : deleter_(deleter) {}

  LatestSlot(const LatestSlot&) = delete;
  LatestSlot& operator=(const LatestSlot&) = delete;

  ~LatestSlot() {
    std::lock_guard<std::mutex> lock(mutex_);
    reset_unlocked();
  }

  void publish(T* item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_ != nullptr) {
      deleter_(slot_);
      ++dropped_;
    }
    slot_ = item;
    cv_.notify_one();
  }

  template <typename Rep, typename Period>
  T* take(const std::atomic<bool>& running, std::chrono::duration<Rep, Period> timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timeout, [&] { return slot_ != nullptr || !running.load(); });
    T* item = slot_;
    slot_ = nullptr;
    return item;
  }

  std::uint64_t dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
  }

  void close() { cv_.notify_all(); }

 private:
  void reset_unlocked() {
    if (slot_ != nullptr) {
      deleter_(slot_);
      slot_ = nullptr;
    }
  }

  Deleter deleter_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  T* slot_ = nullptr;
  std::uint64_t dropped_ = 0;
};

}  // namespace rtx
