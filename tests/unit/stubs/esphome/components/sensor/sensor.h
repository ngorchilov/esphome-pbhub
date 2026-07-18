#pragma once

#include <cstddef>

#include "esphome/core/log.h"

namespace esphome::sensor {

class Sensor {
 public:
  virtual ~Sensor() = default;
  void publish_state(float value) {
    this->state = value;
    this->has_state_ = true;
    this->publish_count_++;
  }

  bool has_state() const { return this->has_state_; }
  size_t test_publish_count() const { return this->publish_count_; }

  float state{0.0f};

 protected:
  bool has_state_{false};
  size_t publish_count_{0};
};

}  // namespace esphome::sensor

#define LOG_SENSOR(prefix, type, sensor) ::esphome::unit_test_log(prefix, type, sensor)
