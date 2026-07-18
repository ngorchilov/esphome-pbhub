#pragma once

#include <array>
#include <cstddef>

#include "pbhub_protocol.h"

namespace esphome::pbhub {

class PbHubRGBWriteClient {
 public:
  virtual ~PbHubRGBWriteClient() = default;

  // Returns true when a transport attempt was made. A stale/deduplicated queue
  // entry returns false so the parent can service a scheduled read instead.
  virtual bool flush_pending_rgb_write() = 0;
};

class PbHubRGBWriteQueue {
 public:
  bool enqueue(PbHubRGBWriteClient *client) {
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

  PbHubRGBWriteClient *pop() {
    if (this->size_ == 0)
      return nullptr;

    auto *client = this->clients_[this->head_];
    this->clients_[this->head_] = nullptr;
    this->head_ = (this->head_ + 1) % this->clients_.size();
    this->size_--;
    return client;
  }

  void clear() {
    this->clients_.fill(nullptr);
    this->head_ = 0;
    this->size_ = 0;
  }

  size_t size() const { return this->size_; }
  bool empty() const { return this->size_ == 0; }

 protected:
  std::array<PbHubRGBWriteClient *, protocol::CHANNEL_COUNT> clients_{};
  size_t head_{0};
  size_t size_{0};
};

}  // namespace esphome::pbhub
