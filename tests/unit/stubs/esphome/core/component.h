#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "esphome/core/log.h"

namespace esphome {

namespace setup_priority {
inline constexpr float IO = 900.0f;
inline constexpr float HARDWARE = 800.0f;
}  // namespace setup_priority

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 600.0f; }

  void status_set_warning() { this->warning_ = true; }
  void status_set_warning(const LogString *) { this->warning_ = true; }
  void status_clear_warning() { this->warning_ = false; }
  bool status_has_warning() const { return this->warning_; }

  void status_set_error() { this->error_ = true; }
  bool status_has_error() const { return this->error_; }

  void mark_failed() {
    this->failed_ = true;
    this->error_ = true;
  }
  void mark_failed(const LogString *) { this->mark_failed(); }
  bool is_failed() const { return this->failed_; }

  void set_timeout(const char *name, uint32_t timeout, std::function<void()> &&callback) {
    this->timeout_name_ = name == nullptr ? "" : name;
    this->timeout_delay_ = timeout;
    this->timeout_callback_ = std::move(callback);
    this->timeout_pending_ = true;
  }

  bool test_has_timeout(const char *name) const {
    return this->timeout_pending_ && this->timeout_name_ == (name == nullptr ? "" : name);
  }

  uint32_t test_timeout_delay(const char *name) const {
    return this->test_has_timeout(name) ? this->timeout_delay_ : 0;
  }

  bool test_run_timeout(const char *name) {
    if (!this->test_has_timeout(name))
      return false;

    auto callback = std::move(this->timeout_callback_);
    this->timeout_pending_ = false;
    this->timeout_name_.clear();
    this->timeout_delay_ = 0;
    callback();
    return true;
  }

 private:
  bool warning_{false};
  bool error_{false};
  bool failed_{false};
  bool timeout_pending_{false};
  uint32_t timeout_delay_{0};
  std::string timeout_name_;
  std::function<void()> timeout_callback_;
};

class PollingComponent : public Component {
 public:
  explicit PollingComponent(uint32_t update_interval) : update_interval_(update_interval) {}
  virtual void update() = 0;

  void set_update_interval(uint32_t update_interval) { this->update_interval_ = update_interval; }
  uint32_t get_update_interval() const { return this->update_interval_; }

 protected:
  uint32_t update_interval_;
};

}  // namespace esphome

#define LOG_UPDATE_INTERVAL(component) ::esphome::unit_test_log((component)->get_update_interval())
