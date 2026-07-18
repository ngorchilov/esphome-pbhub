#include <array>
#include <iostream>
#include <vector>

#include "components/pbhub/pbhub_rgb_queue.h"

using namespace esphome::pbhub;

static int failures = 0;

#define CHECK(...)                                                                                                     \
  do {                                                                                                                 \
    if (!(__VA_ARGS__)) {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #__VA_ARGS__ << '\n';                           \
      failures++;                                                                                                      \
    }                                                                                                                  \
  } while (false)

class FakeRGBClient final : public PbHubRGBWriteClient {
 public:
  FakeRGBClient(int id, std::vector<int> &events) : id_(id), events_(events) {}
  bool flush_pending_rgb_write() override {
    this->events_.push_back(this->id_);
    return true;
  }

 protected:
  int id_;
  std::vector<int> &events_;
};

int main() {
  std::vector<int> events;
  std::array<FakeRGBClient, protocol::CHANNEL_COUNT> clients{{
      {0, events}, {1, events}, {2, events}, {3, events}, {4, events}, {5, events},
  }};
  FakeRGBClient overflow(6, events);
  PbHubRGBWriteQueue queue;

  CHECK(queue.empty());
  CHECK(queue.size() == 0);
  CHECK(!queue.enqueue(nullptr));
  CHECK(queue.pop() == nullptr);

  CHECK(queue.enqueue(&clients[0]));
  CHECK(queue.enqueue(&clients[1]));
  CHECK(queue.enqueue(&clients[0]));
  CHECK(queue.size() == 2);
  CHECK(queue.pop() == &clients[0]);
  CHECK(queue.enqueue(&clients[2]));
  CHECK(queue.pop() == &clients[1]);
  CHECK(queue.pop() == &clients[2]);
  CHECK(queue.empty());

  for (auto &client : clients)
    CHECK(queue.enqueue(&client));
  CHECK(queue.size() == protocol::CHANNEL_COUNT);
  CHECK(!queue.enqueue(&overflow));
  CHECK(queue.enqueue(&clients[3]));

  for (auto &client : clients) {
    auto *next = queue.pop();
    CHECK(next == &client);
    if (next != nullptr)
      next->flush_pending_rgb_write();
  }
  CHECK(events.size() == protocol::CHANNEL_COUNT);
  for (size_t index = 0; index < events.size(); index++)
    CHECK(events[index] == static_cast<int>(index));

  CHECK(queue.enqueue(&clients[4]));
  CHECK(queue.enqueue(&clients[5]));
  queue.clear();
  CHECK(queue.empty());
  CHECK(queue.pop() == nullptr);
  CHECK(queue.enqueue(&clients[0]));
  CHECK(queue.pop() == &clients[0]);

  if (failures != 0)
    std::cerr << failures << " RGB queue checks failed\n";
  return failures == 0 ? 0 : 1;
}
