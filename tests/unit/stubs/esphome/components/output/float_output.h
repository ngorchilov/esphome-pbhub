#pragma once

#include <algorithm>

#include "esphome/core/log.h"

namespace esphome::output {

class FloatOutput {
 public:
  virtual ~FloatOutput() = default;
  void set_level(float state) {
    state = std::clamp(state, 0.0f, 1.0f);
    if (state != 0.0f || !this->zero_means_zero_)
      state = state * (this->max_power_ - this->min_power_) + this->min_power_;
    if (this->inverted_)
      state = 1.0f - state;
    this->write_state(state);
  }
  void turn_on() { this->set_level(1.0f); }
  void turn_off() { this->set_level(0.0f); }
  void set_inverted(bool inverted) { this->inverted_ = inverted; }
  bool is_inverted() const { return this->inverted_; }
  void set_max_power(float max_power) { this->max_power_ = std::clamp(max_power, this->min_power_, 1.0f); }
  void set_min_power(float min_power) { this->min_power_ = std::clamp(min_power, 0.0f, this->max_power_); }
  float get_max_power() const { return this->max_power_; }
  float get_min_power() const { return this->min_power_; }
  void set_zero_means_zero(bool zero_means_zero) { this->zero_means_zero_ = zero_means_zero; }
  virtual void update_frequency(float frequency) { (void) frequency; }
  void test_write_state(float state) { this->write_state(state); }

 protected:
  virtual void write_state(float state) = 0;

  bool inverted_{false};
  float max_power_{1.0f};
  float min_power_{0.0f};
  bool zero_means_zero_{false};
};

}  // namespace esphome::output

#define LOG_FLOAT_OUTPUT(output) ::esphome::unit_test_log(output)
