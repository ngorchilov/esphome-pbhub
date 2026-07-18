#pragma once

#include <array>
#include <cstddef>

#include "pbhub_protocol.h"

namespace esphome::pbhub {

class PbHubPollClient {
 public:
  virtual ~PbHubPollClient() = default;
  virtual void perform_poll() = 0;
};

class PbHubPollQueue {
 public:
  bool enqueue(PbHubPollClient *client) {
    if (client == nullptr)
      return false;

    for (size_t offset = 0; offset < this->size_; offset++) {
      if (this->clients_[(this->head_ + offset) % this->clients_.size()] == client)
        return true;
    }
    if (this->size_ == this->clients_.size())
      return false;

    const size_t tail = (this->head_ + this->size_) % this->clients_.size();
    this->clients_[tail] = client;
    this->size_++;
    return true;
  }

  PbHubPollClient *pop() {
    if (this->size_ == 0)
      return nullptr;

    auto *client = this->clients_[this->head_];
    this->clients_[this->head_] = nullptr;
    this->head_ = (this->head_ + 1) % this->clients_.size();
    this->size_--;
    return client;
  }

  size_t size() const { return this->size_; }
  bool empty() const { return this->size_ == 0; }

 protected:
  std::array<PbHubPollClient *, protocol::ENDPOINT_COUNT> clients_{};
  size_t head_{0};
  size_t size_{0};
};

}  // namespace esphome::pbhub
