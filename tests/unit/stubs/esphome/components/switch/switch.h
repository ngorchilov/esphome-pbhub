#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "esphome/core/log.h"

namespace esphome::switch_ {

enum SwitchRestoreMode : uint8_t {
  SWITCH_ALWAYS_OFF = 0,
  SWITCH_ALWAYS_ON = 1,
  SWITCH_RESTORE_DEFAULT_OFF = 2,
  SWITCH_RESTORE_DEFAULT_ON = 3,
  SWITCH_RESTORE_INVERTED_DEFAULT_OFF = 6,
  SWITCH_RESTORE_INVERTED_DEFAULT_ON = 7,
  SWITCH_RESTORE_DISABLED = 8,
};

inline constexpr uint8_t RESTORE_MODE_ON_MASK = 0x01;
inline constexpr uint8_t RESTORE_MODE_PERSISTENT_MASK = 0x02;
inline constexpr uint8_t RESTORE_MODE_INVERTED_MASK = 0x04;
inline constexpr uint8_t RESTORE_MODE_DISABLED_MASK = 0x08;

class Switch {
 public:
  virtual ~Switch() = default;

  void turn_on() { this->write_state(!this->inverted_); }
  void turn_off() { this->write_state(this->inverted_); }

  void publish_state(bool raw_state) {
    if (this->published_raw_known_ && this->published_raw_ == raw_state)
      return;
    this->published_raw_ = raw_state;
    this->published_raw_known_ = true;
    this->state = raw_state != this->inverted_;
    if (this->restore_mode & RESTORE_MODE_PERSISTENT_MASK)
      this->restored_state_ = this->state;
    this->has_state_ = true;
    this->publish_count_++;
  }

  void set_inverted(bool inverted) { this->inverted_ = inverted; }
  bool is_inverted() const { return this->inverted_; }
  void set_restore_mode(SwitchRestoreMode mode) { this->restore_mode = mode; }
  void test_set_restored_state(std::optional<bool> state) { this->restored_state_ = state; }

  std::optional<bool> get_initial_state_with_restore_mode() const {
    if (this->restore_mode & RESTORE_MODE_DISABLED_MASK)
      return {};
    bool initial_state = (this->restore_mode & RESTORE_MODE_ON_MASK) != 0;
    if ((this->restore_mode & RESTORE_MODE_PERSISTENT_MASK) && this->restored_state_.has_value()) {
      initial_state = (this->restore_mode & RESTORE_MODE_INVERTED_MASK) ? !this->restored_state_.value()
                                                                       : this->restored_state_.value();
    }
    return initial_state;
  }

  virtual bool assumed_state() { return false; }
  bool has_state() const { return this->has_state_; }
  size_t test_publish_count() const { return this->publish_count_; }

  SwitchRestoreMode restore_mode{SWITCH_ALWAYS_OFF};
  bool state{false};

 protected:
  virtual void write_state(bool state) = 0;

  bool inverted_{false};
  bool has_state_{false};
  bool published_raw_known_{false};
  bool published_raw_{false};
  size_t publish_count_{0};
  std::optional<bool> restored_state_{};
};

}  // namespace esphome::switch_

#define LOG_SWITCH(prefix, type, switch_obj) ::esphome::unit_test_log(prefix, type, switch_obj)
