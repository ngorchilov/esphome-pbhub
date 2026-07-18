#include <array>
#include <iostream>
#include <vector>

#include "components/pbhub/pbhub_polling.h"

using namespace esphome::pbhub;

static int failures = 0;

#define CHECK(...)                                                                                                     \
  do {                                                                                                                 \
    if (!(__VA_ARGS__)) {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #__VA_ARGS__ << '\n';                           \
      failures++;                                                                                                      \
    }                                                                                                                  \
  } while (false)

class FakePollClient final : public PbHubPollClient {
 public:
  FakePollClient(int id, std::vector<int> &events) : id_(id), events_(events) {}
  void perform_poll() override { this->events_.push_back(this->id_); }

 protected:
  int id_;
  std::vector<int> &events_;
};

int main() {
  std::vector<int> events;
  std::array<FakePollClient, protocol::ENDPOINT_COUNT> clients{{
      {0, events}, {1, events}, {2, events}, {3, events}, {4, events},  {5, events},
      {6, events}, {7, events}, {8, events}, {9, events}, {10, events}, {11, events},
  }};
  FakePollClient overflow(12, events);
  PbHubPollQueue queue;

  CHECK(queue.empty());
  CHECK(queue.size() == 0);
  CHECK(!queue.enqueue(nullptr));
  CHECK(queue.pop() == nullptr);

  CHECK(queue.enqueue(&clients[0]));
  CHECK(queue.enqueue(&clients[1]));
  CHECK(queue.enqueue(&clients[0]));
  CHECK(queue.size() == 2);
  CHECK(queue.pop() == &clients[0]);
  CHECK(queue.pop() == &clients[1]);
  CHECK(queue.empty());

  for (auto &client : clients)
    CHECK(queue.enqueue(&client));
  CHECK(queue.size() == protocol::ENDPOINT_COUNT);
  CHECK(!queue.enqueue(&overflow));
  CHECK(queue.enqueue(&clients[5]));
  CHECK(queue.size() == protocol::ENDPOINT_COUNT);

  for (auto &client : clients) {
    auto *next = queue.pop();
    CHECK(next == &client);
    if (next != nullptr)
      next->perform_poll();
  }
  CHECK(queue.empty());
  CHECK(events.size() == protocol::ENDPOINT_COUNT);
  for (size_t index = 0; index < events.size(); index++)
    CHECK(events[index] == static_cast<int>(index));

  CHECK(queue.enqueue(&clients[10]));
  CHECK(queue.enqueue(&clients[11]));
  CHECK(queue.pop() == &clients[10]);
  CHECK(queue.enqueue(&clients[0]));
  CHECK(queue.pop() == &clients[11]);
  CHECK(queue.pop() == &clients[0]);
  CHECK(queue.empty());

  if (failures != 0)
    std::cerr << failures << " polling checks failed\n";
  return failures == 0 ? 0 : 1;
}
