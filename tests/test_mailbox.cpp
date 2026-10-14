#include "mailbox.hpp"
#include "check.hpp"

#include <atomic>
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
  CHECK(slot.dropped() == 1);
  CHECK(g_deleted == 1);
  std::atomic<bool> running{true};
  int* value = slot.take(running, std::chrono::milliseconds(20));
  CHECK(value != nullptr && *value == 2);
  destroy_int(value);
  running.store(false);
  CHECK(slot.take(running, std::chrono::milliseconds(20)) == nullptr);
  CHECK(g_deleted == 2);
  return rtx_test::result();
}
