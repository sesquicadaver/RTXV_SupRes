#include "mailbox.hpp"

#include <atomic>
#include <cassert>
#include <chrono>

namespace {

int g_deleted = 0;

void destroy_int(int* value) {
  ++g_deleted;
  delete value;
}

}  // namespace

int main() {
  rtx::LatestSlot<int> slot(destroy_int);
  slot.publish(new int(1));
  slot.publish(new int(2));
  assert(slot.dropped() == 1);
  assert(g_deleted == 1);
  std::atomic<bool> running{true};
  int* value = slot.take(running, std::chrono::milliseconds(20));
  assert(value != nullptr && *value == 2);
  destroy_int(value);
  running.store(false);
  assert(slot.take(running, std::chrono::milliseconds(20)) == nullptr);
  assert(g_deleted == 2);
  return 0;
}
