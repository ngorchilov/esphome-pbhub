#pragma once

#include <cstddef>

#include "esphome/core/log.h"

namespace esphome::binary_sensor {

class BinarySensor {
 public:
  virtual ~BinarySensor() = default;

  void publish_state(bool new_state) {
    if (this->has_state_ && this->state == new_state)
      return;
    this->state = new_state;
    this->has_state_ = true;
    this->publish_count_++;
  }

  bool has_state() const { return this->has_state_; }
  size_t test_publish_count() const { return this->publish_count_; }

  bool state{false};

 protected:
  bool has_state_{false};
  size_t publish_count_{0};
};

}  // namespace esphome::binary_sensor

#define LOG_BINARY_SENSOR(prefix, type, sensor) ::esphome::unit_test_log(prefix, type, sensor)
