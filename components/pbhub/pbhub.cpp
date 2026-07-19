#include "pbhub.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pbhub {

static const char *const TAG = "pbhub";
static constexpr uint32_t RECOVERY_RETRY_INTERVAL_MS = 5000;
static constexpr uint32_t FAILURE_LOG_INTERVAL_MS = 30000;

static const char *hub_state_name(HubState state) {
  switch (state) {
    case HubState::UNVERIFIED:
      return "UNVERIFIED";
    case HubState::RECOVERING:
      return "RECOVERING";
    case HubState::READY:
      return "READY";
    case HubState::UNSUPPORTED:
      return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

static const char *endpoint_owner_name(EndpointOwner owner) {
  switch (owner) {
    case EndpointOwner::PWM:
      return "PWM output";
    case EndpointOwner::SERVO:
      return "servo output";
    case EndpointOwner::ADC:
      return "ADC sensor";
    case EndpointOwner::RGB:
      return "RGB light";
    case EndpointOwner::DIGITAL_INPUT:
      return "digital input";
    case EndpointOwner::DIGITAL_OUTPUT:
      return "digital output";
    case EndpointOwner::NONE:
      return "none";
  }
  return "unknown";
}

static char signal_name(uint8_t signal_index) {
  if (signal_index == protocol::SIGNAL_A_INDEX)
    return 'A';
  if (signal_index == protocol::SIGNAL_B_INDEX)
    return 'B';
  return '?';
}

static const char *i2c_error_name(i2c::ErrorCode error) {
  switch (error) {
    case i2c::ERROR_OK:
      return "ok";
    case i2c::ERROR_INVALID_ARGUMENT:
      return "invalid argument";
    case i2c::ERROR_NOT_ACKNOWLEDGED:
      return "not acknowledged";
    case i2c::ERROR_TIMEOUT:
      return "timeout";
    case i2c::ERROR_NOT_INITIALIZED:
      return "not initialized";
    case i2c::ERROR_TOO_LARGE:
      return "too large";
    case i2c::ERROR_UNKNOWN:
      return "unknown";
    case i2c::ERROR_CRC:
      return "CRC";
  }
  return "unrecognized";
}

void PbHubComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PBHUB at 0x%02X", this->address_);
  if (this->configuration_error_) {
    ESP_LOGE(TAG, "PBHUB configuration contains an invalid or duplicate endpoint claim");
    this->mark_failed(LOG_STR("PBHUB endpoint ownership conflict"));
    return;
  }
  this->attempt_recovery_();
}

void PbHubComponent::loop() {
  if (!this->is_hub_ready())
    return;

  const bool rgb_available = this->rgb_refresh_due_() && !this->rgb_write_queue_.empty();
  const bool poll_available = !this->poll_queue_.empty();

  if (poll_available && (!rgb_available || this->prefer_poll_after_rgb_)) {
    auto *poll_client = this->poll_queue_.pop();
    if (poll_client != nullptr) {
      this->prefer_poll_after_rgb_ = false;
      poll_client->perform_poll();
      return;
    }
  }

  if (rgb_available) {
    auto *rgb_client = this->rgb_write_queue_.pop();
    if (rgb_client != nullptr && rgb_client->flush_pending_rgb_write()) {
      this->prefer_poll_after_rgb_ = true;
      return;
    }
  }

  auto *poll_client = this->poll_queue_.pop();
  if (poll_client != nullptr) {
    this->prefer_poll_after_rgb_ = false;
    poll_client->perform_poll();
  }
}

void PbHubComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "PBHUB:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  State: %s", hub_state_name(this->recovery_.state()));
  if (this->firmware_version_ != 0)
    ESP_LOGCONFIG(TAG, "  Firmware protocol version: %u", this->firmware_version_);
  else
    ESP_LOGCONFIG(TAG, "  Firmware protocol version: unverified");
  ESP_LOGCONFIG(TAG, "  Recovery clients: %u", static_cast<unsigned>(this->recovery_.client_count()));
  ESP_LOGCONFIG(TAG, "  Normal RGB fill limit: %u ms between fills per hub (provisional)",
                static_cast<unsigned>(RGB_MIN_REFRESH_INTERVAL_US / 1000));
  ESP_LOGCONFIG(TAG, "  Communication/protocol failures: %u total, %u consecutive",
                static_cast<unsigned>(this->total_failures_), static_cast<unsigned>(this->consecutive_failures_));
}

bool PbHubComponent::register_recovery_client(PbHubRecoveryClient *client) {
  if (this->recovery_.register_client(client))
    return true;
  ESP_LOGE(TAG, "Rejected invalid or duplicate PBHUB recovery client registration");
  this->configuration_error_ = true;
  return false;
}

bool PbHubComponent::queue_poll(PbHubPollClient *client) {
  if (this->poll_queue_.enqueue(client))
    return true;
  ESP_LOGE(TAG, "Rejected invalid or full PBHUB scheduled-read queue");
  return false;
}

bool PbHubComponent::queue_rgb_write(PbHubRGBWriteClient *client) {
  if (!this->is_hub_ready())
    return false;
  if (this->rgb_write_queue_.enqueue(client))
    return true;
  ESP_LOGE(TAG, "Rejected invalid or full PBHUB RGB-write queue");
  return false;
}

bool PbHubComponent::claim_endpoint(uint8_t channel, uint8_t signal_index, EndpointOwner owner,
                                    const char *owner_id) {
  const protocol::Endpoint endpoint{channel, signal_index};
  if (!can_own_endpoint(endpoint, owner) || owner_id == nullptr) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB %s claim for channel %u signal index %u", endpoint_owner_name(owner),
             channel, signal_index);
    this->configuration_error_ = true;
    return false;
  }

  const auto *existing = this->endpoint_claims_.find(endpoint);
  if (existing == nullptr) {
    if (!this->endpoint_claims_.claim(endpoint, owner, owner_id)) {
      ESP_LOGE(TAG, "Rejected invalid PBHUB endpoint claim for channel %u signal %c", channel,
               signal_name(signal_index));
      this->configuration_error_ = true;
      return false;
    }
    return true;
  }

  ESP_LOGE(TAG, "PBHUB channel %u signal %c is claimed by both %s '%s' and %s '%s'", channel,
           signal_name(signal_index), endpoint_owner_name(existing->owner), existing->owner_id,
           endpoint_owner_name(owner), owner_id);
  this->endpoint_claims_.claim(endpoint, owner, owner_id);
  this->configuration_error_ = true;
  return false;
}

void PbHubComponent::set_led_timing_mode(uint8_t mode) {
  protocol::WriteCommand<1> command{};
  if (!protocol::make_led_timing_write(mode, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB LED timing mode %u", mode);
    this->configuration_error_ = true;
    return;
  }
  this->led_timing_configured_ = true;
  this->led_timing_mode_ = mode;
}

bool PbHubComponent::feature_io_allowed_() const {
  return !this->configuration_error_ &&
         (this->recovery_.state() == HubState::READY ||
          (this->recovery_.state() == HubState::RECOVERING && this->recovery_io_active_));
}

bool PbHubComponent::endpoint_owned_by_(protocol::Endpoint endpoint, EndpointOwner owner, const char *operation) const {
  if (!this->endpoint_claims_.owns(endpoint, owner)) {
    ESP_LOGE(TAG, "%s rejected because its PBHUB endpoint is not claimed by %s", operation, endpoint_owner_name(owner));
    return false;
  }
  return true;
}

bool PbHubComponent::read_transaction_(const char *operation, const protocol::ReadCommand &command, uint8_t *data,
                                       size_t length) {
  if (command.response_length != length) {
    ESP_LOGE(TAG, "%s has an internal response-length mismatch for register 0x%02X", operation, command.reg);
    return false;
  }

  const auto error = i2c::I2CDevice::read_register(command.reg, data, length);
  if (error != i2c::ERROR_OK) {
    this->handle_transport_failure_(operation, command.reg, error);
    return false;
  }
  this->handle_transport_success_();
  return true;
}

bool PbHubComponent::write_transaction_(const char *operation, uint8_t reg, const uint8_t *data, size_t length) {
  const auto error = i2c::I2CDevice::write_register(reg, data, length);
  if (error != i2c::ERROR_OK) {
    this->handle_transport_failure_(operation, reg, error);
    return false;
  }
  this->handle_transport_success_();
  return true;
}

void PbHubComponent::handle_transport_failure_(const char *operation, uint8_t reg, i2c::ErrorCode error) {
  const uint32_t now = millis();
  this->consecutive_failures_++;
  this->total_failures_++;

  if (!this->failure_logged_ || now - this->last_failure_log_ms_ >= FAILURE_LOG_INTERVAL_MS) {
    ESP_LOGW(TAG, "%s failed at register 0x%02X: I2C error %u (%s)", operation, reg,
             static_cast<unsigned>(error), i2c_error_name(error));
    this->last_failure_log_ms_ = now;
    this->failure_logged_ = true;
  } else {
    ESP_LOGV(TAG, "%s failed at register 0x%02X: I2C error %u", operation, reg, static_cast<unsigned>(error));
  }

  this->recovery_.transport_failed();
  this->status_set_warning(LOG_STR("PBHUB communication failure"));
  this->schedule_recovery_();
}

void PbHubComponent::handle_protocol_failure_(const char *operation, uint8_t reg, const char *reason) {
  const uint32_t now = millis();
  this->consecutive_failures_++;
  this->total_failures_++;
  if (!this->failure_logged_ || now - this->last_failure_log_ms_ >= FAILURE_LOG_INTERVAL_MS) {
    ESP_LOGW(TAG, "%s returned an invalid response at register 0x%02X: %s", operation, reg, reason);
    this->last_failure_log_ms_ = now;
    this->failure_logged_ = true;
  } else {
    ESP_LOGV(TAG, "%s returned an invalid response at register 0x%02X: %s", operation, reg, reason);
  }
  this->recovery_.transport_failed();
  this->status_set_warning(LOG_STR("PBHUB protocol response failure"));
  this->schedule_recovery_();
}

void PbHubComponent::handle_transport_success_() { this->consecutive_failures_ = 0; }

void PbHubComponent::schedule_recovery_() {
  if (this->configuration_error_ || this->recovery_scheduled_ || this->recovery_.state() != HubState::UNVERIFIED)
    return;

  this->recovery_scheduled_ = true;
  this->set_timeout("firmware_probe", RECOVERY_RETRY_INTERVAL_MS, [this]() {
    this->recovery_scheduled_ = false;
    this->attempt_recovery_();
  });
}

void PbHubComponent::attempt_recovery_() {
  this->recovery_scheduled_ = false;
  this->recovery_io_active_ = true;
  const bool recovered = this->recovery_.attempt_recovery(*this);
  this->recovery_io_active_ = false;
  if (recovered) {
    // Recovery replay has already applied every known desired RGB state. Drop
    // queued pre-failure entries so they cannot consume later scheduler turns.
    this->rgb_write_queue_.clear();
    this->status_clear_warning();
    this->failure_logged_ = false;
    if (this->ever_ready_)
      ESP_LOGI(TAG, "PBHUB communication recovered; firmware protocol version %u verified", this->firmware_version_);
    else
      ESP_LOGCONFIG(TAG, "  Firmware protocol version: %u", this->firmware_version_);
    this->ever_ready_ = true;
    this->recovery_.notify_recovery_complete();
    return;
  }

  if (this->recovery_.state() == HubState::UNSUPPORTED) {
    ESP_LOGE(TAG, "Unsupported PBHUB firmware protocol version %u; expected exactly %u", this->firmware_version_,
             protocol::EXPECTED_FIRMWARE_VERSION);
    this->status_clear_warning();
    this->mark_failed(LOG_STR("Unsupported PBHUB firmware version"));
    return;
  }

  this->status_set_warning(LOG_STR("PBHUB firmware version unverified"));
  this->schedule_recovery_();
}

bool PbHubComponent::rgb_refresh_due_() const {
  return !this->rgb_refresh_started_ || micros() - this->last_rgb_refresh_us_ >= RGB_MIN_REFRESH_INTERVAL_US;
}

FirmwareProbeResult PbHubComponent::probe_firmware() {
  const auto command = protocol::firmware_version_read();
  std::array<uint8_t, 1> response{};
  if (!this->read_transaction_("firmware version probe", command, response.data(), response.size()))
    return FirmwareProbeResult::TRANSPORT_FAILURE;

  this->firmware_version_ = response[0];
  return protocol::is_supported_firmware(this->firmware_version_) ? FirmwareProbeResult::SUPPORTED
                                                                  : FirmwareProbeResult::UNSUPPORTED;
}

bool PbHubComponent::restore_global_configuration() {
  if (!this->led_timing_configured_)
    return true;
  return this->write_led_timing_mode_(this->led_timing_mode_);
}

bool PbHubComponent::write_led_timing_mode_(uint8_t mode) {
  protocol::WriteCommand<1> command{};
  if (!protocol::make_led_timing_write(mode, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB LED timing mode %u", mode);
    return false;
  }
  return this->write_command_("LED timing write", command);
}

bool PbHubComponent::read_digital(protocol::Endpoint endpoint, bool &value) {
  protocol::ReadCommand command{};
  if (!protocol::make_digital_read(endpoint, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB digital-read endpoint");
    return false;
  }
  if (!this->endpoint_owned_by_(endpoint, EndpointOwner::DIGITAL_INPUT, "digital read") ||
      !this->feature_io_allowed_())
    return false;

  std::array<uint8_t, 1> response{};
  if (!this->read_transaction_("digital read", command, response.data(), response.size()))
    return false;
  if (!protocol::decode_digital(response, value)) {
    this->handle_protocol_failure_("digital read", command.reg, "expected 0 or 1");
    return false;
  }
  return true;
}

bool PbHubComponent::write_digital(protocol::Endpoint endpoint, bool value) {
  protocol::WriteCommand<1> command{};
  if (!protocol::make_digital_write(endpoint, value, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB digital-write endpoint");
    return false;
  }
  if (!this->endpoint_owned_by_(endpoint, EndpointOwner::DIGITAL_OUTPUT, "digital write"))
    return false;
  return this->write_command_("digital write", command);
}

bool PbHubComponent::read_adc(uint8_t channel, uint16_t &value) {
  protocol::ReadCommand command{};
  if (!protocol::make_adc_read(channel, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB ADC channel %u", channel);
    return false;
  }
  if (!this->endpoint_owned_by_({channel, protocol::SIGNAL_A_INDEX}, EndpointOwner::ADC, "ADC read") ||
      !this->feature_io_allowed_())
    return false;

  std::array<uint8_t, 2> response{};
  if (!this->read_transaction_("ADC read", command, response.data(), response.size()))
    return false;
  if (!protocol::decode_adc(response, value)) {
    this->handle_protocol_failure_("ADC read", command.reg, "expected a 12-bit value");
    return false;
  }
  return true;
}

bool PbHubComponent::write_pwm(protocol::Endpoint endpoint, uint8_t duty) {
  protocol::WriteCommand<1> command{};
  if (!protocol::make_pwm_write(endpoint, duty, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB PWM endpoint");
    return false;
  }
  if (!this->endpoint_owned_by_(endpoint, EndpointOwner::PWM, "PWM write"))
    return false;
  return this->write_command_("PWM write", command);
}

bool PbHubComponent::write_servo_pulse(protocol::Endpoint endpoint, uint16_t pulse_us) {
  protocol::WriteCommand<2> command{};
  if (!protocol::make_servo_pulse_write(endpoint, pulse_us, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB servo endpoint or pulse %u", pulse_us);
    return false;
  }
  if (!this->endpoint_owned_by_(endpoint, EndpointOwner::SERVO, "servo pulse write"))
    return false;
  return this->write_command_("servo pulse write", command);
}

bool PbHubComponent::write_servo_detach(protocol::Endpoint endpoint) {
  protocol::WriteCommand<1> command{};
  if (!protocol::make_servo_detach_write(endpoint, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB servo-detach endpoint");
    return false;
  }
  if (!this->endpoint_owned_by_(endpoint, EndpointOwner::SERVO, "servo detach"))
    return false;
  return this->write_command_("servo detach", command);
}

bool PbHubComponent::configure_leds(uint8_t channel, uint16_t count) {
  protocol::WriteCommand<2> command{};
  if (!protocol::make_led_count_write(channel, count, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB LED configuration: channel=%u count=%u", channel, count);
    return false;
  }
  if (!this->endpoint_owned_by_({channel, protocol::SIGNAL_B_INDEX}, EndpointOwner::RGB, "LED count write"))
    return false;
  return this->write_command_("LED count write", command);
}

bool PbHubComponent::set_led_full_brightness(uint8_t channel) {
  protocol::WriteCommand<1> command{};
  if (!protocol::make_led_full_brightness_write(channel, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB LED channel %u", channel);
    return false;
  }
  if (!this->endpoint_owned_by_({channel, protocol::SIGNAL_B_INDEX}, EndpointOwner::RGB, "LED brightness write"))
    return false;
  return this->write_command_("LED brightness write", command);
}

bool PbHubComponent::fill_leds(uint8_t channel, uint16_t configured_count, uint16_t start, uint16_t count, uint8_t red,
                               uint8_t green, uint8_t blue) {
  protocol::WriteCommand<7> command{};
  if (!protocol::make_led_fill_write(channel, configured_count, start, count, {red, green, blue}, command)) {
    ESP_LOGE(TAG, "Rejected unsafe PBHUB LED fill: channel=%u configured=%u start=%u count=%u", channel,
             configured_count, start, count);
    return false;
  }
  if (!this->endpoint_owned_by_({channel, protocol::SIGNAL_B_INDEX}, EndpointOwner::RGB, "LED fill write"))
    return false;
  if (!this->write_command_("LED fill write", command))
    return false;
  this->last_rgb_refresh_us_ = micros();
  this->rgb_refresh_started_ = true;
  return true;
}

#ifdef USE_BINARY_SENSOR
PbHubBinarySensor::PbHubBinarySensor(PbHubComponent *parent, uint8_t channel, uint8_t signal_index,
                                     uint32_t update_interval)
    : PollingComponent(update_interval),
      parent_(parent),
      endpoint_{channel, signal_index},
      endpoint_valid_(protocol::is_valid(this->endpoint_)) {}

void PbHubBinarySensor::update() {
  if (this->parent_ != nullptr)
    this->parent_->queue_poll(this);
}

void PbHubBinarySensor::perform_poll() {
  if (this->parent_ == nullptr || !this->endpoint_valid_)
    return;
  bool raw;
  if (this->parent_->read_digital(this->endpoint_, raw))
    this->publish_state(raw != this->inverted_);
}

void PbHubBinarySensor::dump_config() {
  LOG_BINARY_SENSOR("", "PBHUB Binary Sensor", this);
  ESP_LOGCONFIG(TAG, "  Channel: %u", this->endpoint_.channel);
  ESP_LOGCONFIG(TAG, "  Signal: %c", signal_name(this->endpoint_.index));
  LOG_UPDATE_INTERVAL(this);
}
#endif

#ifdef USE_OUTPUT
PbHubPWMOutput::PbHubPWMOutput(PbHubComponent *parent, uint8_t channel, uint8_t signal_index)
    : parent_(parent), endpoint_{channel, signal_index}, endpoint_valid_(protocol::is_valid(this->endpoint_)) {}

void PbHubPWMOutput::setup() {
  if (!this->desired_known_)
    this->set_level(0.0f);
}

void PbHubPWMOutput::loop() {
  if (!this->note_gap_pending_ || millis() - this->note_gap_started_ms_ < FIXED_TONE_NOTE_GAP_MS)
    return;

  this->note_gap_pending_ = false;
  if (this->parent_ != nullptr && this->parent_->is_hub_ready())
    this->apply_desired_state_();
}

void PbHubPWMOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "PBHUB PWM Output:");
  if (this->endpoint_valid_) {
    ESP_LOGCONFIG(TAG, "  Channel: %u", this->endpoint_.channel);
    ESP_LOGCONFIG(TAG, "  Signal: %c", signal_name(this->endpoint_.index));
  } else {
    ESP_LOGCONFIG(TAG, "  Channel/signal: invalid");
  }
  ESP_LOGCONFIG(TAG, "  PWM Frequency: nominal %.2f Hz (fixed by firmware)", protocol::NOMINAL_PWM_FREQUENCY_HZ);
  ESP_LOGCONFIG(TAG, "  RTTTL: fixed-tone rhythm supported; requested note frequencies are ignored");
  LOG_FLOAT_OUTPUT(this);
}

void PbHubPWMOutput::update_frequency(float frequency) {
  (void) frequency;
  if (this->parent_ == nullptr || !this->parent_->is_hub_ready() || !this->endpoint_valid_ ||
      !this->desired_known_ || this->desired_mode_ == protocol::PwmDriveMode::DIGITAL_LOW ||
      !this->applied_known_ || this->applied_mode_ == protocol::PwmDriveMode::DIGITAL_LOW ||
      this->note_gap_pending_)
    return;

  if (!this->parent_->write_pwm(this->endpoint_, 0))
    return;

  this->applied_duty_ = 0;
  this->applied_mode_ = protocol::PwmDriveMode::DIGITAL_LOW;
  this->applied_known_ = true;
  this->note_gap_pending_ = true;
  this->note_gap_started_ms_ = millis();
}

void PbHubPWMOutput::write_state(float state) {
  if (this->parent_ == nullptr)
    return;
  if (!std::isfinite(state)) {
    if (!this->invalid_level_warning_logged_) {
      ESP_LOGW(TAG, "Ignored non-finite PBHUB PWM level");
      this->invalid_level_warning_logged_ = true;
    }
    return;
  }

  const float clamped = std::clamp(state, 0.0f, 1.0f);
  this->desired_duty_ = static_cast<uint8_t>(std::lround(clamped * 255.0f));
  this->desired_mode_ = protocol::pwm_drive_mode(this->desired_duty_);
  this->desired_known_ = true;
  if (this->desired_mode_ == protocol::PwmDriveMode::DIGITAL_LOW)
    this->note_gap_pending_ = false;
  if (this->parent_->is_hub_ready() && !this->note_gap_pending_)
    this->apply_desired_state_();
}

bool PbHubPWMOutput::replay_state() { return !this->desired_known_ || this->apply_desired_state_(); }

bool PbHubPWMOutput::apply_desired_state_() {
  if (this->applied_known_ && this->applied_duty_ == this->desired_duty_ &&
      this->applied_mode_ == this->desired_mode_)
    return true;
  if (!this->endpoint_valid_ || !this->parent_->write_pwm(this->endpoint_, this->desired_duty_))
    return false;
  this->applied_duty_ = this->desired_duty_;
  this->applied_mode_ = this->desired_mode_;
  this->applied_known_ = true;
  return true;
}

PbHubServoOutput::PbHubServoOutput(PbHubComponent *parent, uint8_t channel, uint8_t signal_index)
    : parent_(parent), endpoint_{channel, signal_index}, endpoint_valid_(protocol::is_valid(this->endpoint_)) {}

void PbHubServoOutput::setup() {
  if (!this->desired_known_)
    this->write_state(0.0f);
}

void PbHubServoOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "PBHUB Servo Output:");
  if (this->endpoint_valid_) {
    ESP_LOGCONFIG(TAG, "  Channel: %u", this->endpoint_.channel);
    ESP_LOGCONFIG(TAG, "  Signal: %c", signal_name(this->endpoint_.index));
  } else {
    ESP_LOGCONFIG(TAG, "  Channel/signal: invalid");
  }
  ESP_LOGCONFIG(TAG, "  Frame Frequency: nominal %.2f Hz (fixed by firmware)",
                protocol::NOMINAL_SERVO_FREQUENCY_HZ);
  ESP_LOGCONFIG(TAG, "  Pulse Range: %u..%u us; zero detaches", protocol::SERVO_MIN_PULSE_US,
                protocol::SERVO_MAX_PULSE_US);
  LOG_FLOAT_OUTPUT(this);
}

void PbHubServoOutput::update_frequency(float frequency) {
  if (this->frequency_warning_logged_)
    return;
  this->frequency_warning_logged_ = true;
  ESP_LOGW(TAG, "PBHUB servo frequency is fixed at nominal %.2f Hz; requested %.2f Hz was ignored",
           protocol::NOMINAL_SERVO_FREQUENCY_HZ, frequency);
}

void PbHubServoOutput::write_state(float state) {
  if (this->parent_ == nullptr)
    return;
  if (!std::isfinite(state)) {
    if (!this->nonfinite_warning_logged_) {
      ESP_LOGW(TAG, "Ignored non-finite PBHUB servo level");
      this->nonfinite_warning_logged_ = true;
    }
    return;
  }

  uint16_t pulse_us = 0;
  if (state != 0.0f) {
    // FloatOutput normally guarantees this domain. Keep the check here so a
    // future direct caller cannot overflow lround() before pulse validation.
    if (state < 0.0f || state > 1.0f) {
      if (!this->invalid_level_warning_logged_) {
        ESP_LOGW(TAG, "Ignored PBHUB servo level %.4f outside the FloatOutput range", state);
        this->invalid_level_warning_logged_ = true;
      }
      return;
    }

    bool transforms_neutral = !this->is_inverted();
#ifdef USE_OUTPUT_FLOAT_POWER_SCALING
    transforms_neutral = transforms_neutral && this->get_min_power() == 0.0f && this->get_max_power() == 1.0f &&
                         this->zero_means_zero_;
#endif
    if (!transforms_neutral) {
      if (!this->invalid_transform_warning_logged_) {
        ESP_LOGW(TAG, "Ignored PBHUB servo level because output transforms are no longer neutral");
        this->invalid_transform_warning_logged_ = true;
      }
      return;
    }

    const long rounded_pulse = std::lround(state * protocol::SERVO_FRAME_US);
    if (rounded_pulse < protocol::SERVO_MIN_PULSE_US || rounded_pulse > protocol::SERVO_MAX_PULSE_US) {
      if (!this->invalid_level_warning_logged_) {
        ESP_LOGW(TAG,
                 "Ignored PBHUB servo level %.4f: rounded pulse %ld us is outside the firmware-accepted range",
                 state, rounded_pulse);
        this->invalid_level_warning_logged_ = true;
      }
      return;
    }
    pulse_us = static_cast<uint16_t>(rounded_pulse);
  }

  this->desired_pulse_us_ = pulse_us;
  this->desired_known_ = true;
  if (this->parent_->is_hub_ready())
    this->apply_desired_state_();
}

bool PbHubServoOutput::replay_state() { return !this->desired_known_ || this->apply_desired_state_(); }

bool PbHubServoOutput::apply_desired_state_() {
  if (this->applied_known_ && this->applied_pulse_us_ == this->desired_pulse_us_)
    return true;
  if (!this->endpoint_valid_)
    return false;

  const bool success = this->desired_pulse_us_ == 0 ? this->parent_->write_servo_detach(this->endpoint_)
                                                    : this->parent_->write_servo_pulse(this->endpoint_,
                                                                                     this->desired_pulse_us_);
  if (!success)
    return false;
  this->applied_pulse_us_ = this->desired_pulse_us_;
  this->applied_known_ = true;
  return true;
}
#endif

#ifdef USE_SENSOR
PbHubADC::PbHubADC(PbHubComponent *parent, uint8_t channel, uint32_t update_interval)
    : PollingComponent(update_interval), parent_(parent), channel_(channel) {}

void PbHubADC::update() {
  if (this->parent_ != nullptr)
    this->parent_->queue_poll(this);
}

void PbHubADC::perform_poll() {
  if (this->parent_ == nullptr)
    return;
  uint16_t value;
  if (this->parent_->read_adc(this->channel_, value))
    this->publish_state(value);
}

void PbHubADC::dump_config() {
  LOG_SENSOR("", "PBHUB ADC", this);
  ESP_LOGCONFIG(TAG, "  Channel: %u", this->channel_);
  ESP_LOGCONFIG(TAG, "  Signal: A (fixed by firmware)");
  LOG_UPDATE_INTERVAL(this);
}
#endif

#ifdef USE_SWITCH
PbHubSwitch::PbHubSwitch(PbHubComponent *parent, uint8_t channel, uint8_t signal_index)
    : parent_(parent), endpoint_{channel, signal_index}, endpoint_valid_(protocol::is_valid(this->endpoint_)) {}

void PbHubSwitch::setup() {
  const auto initial_state = this->get_initial_state_with_restore_mode();
  if (!initial_state.has_value())
    return;
  if (initial_state.value())
    this->turn_on();
  else
    this->turn_off();
}

void PbHubSwitch::dump_config() {
  LOG_SWITCH("", "PBHUB Switch", this);
  ESP_LOGCONFIG(TAG, "  Channel: %u", this->endpoint_.channel);
  ESP_LOGCONFIG(TAG, "  Signal: %c", signal_name(this->endpoint_.index));
}

void PbHubSwitch::write_state(bool state) {
  this->desired_raw_ = state;
  this->desired_known_ = true;
  if (this->parent_ != nullptr && this->parent_->is_hub_ready())
    this->apply_desired_state_(true);
}

bool PbHubSwitch::replay_state() { return !this->desired_known_ || this->apply_desired_state_(false); }

bool PbHubSwitch::apply_desired_state_(bool publish_immediately) {
  if (this->applied_known_ && this->applied_raw_ == this->desired_raw_)
    return true;

  if (this->parent_ == nullptr || !this->endpoint_valid_ ||
      !this->parent_->write_digital(this->endpoint_, this->desired_raw_))
    return false;

  this->applied_raw_ = this->desired_raw_;
  this->applied_known_ = true;
  if (publish_immediately) {
    this->publish_pending_ = false;
    this->publish_state(this->applied_raw_);
  } else {
    this->pending_raw_ = this->applied_raw_;
    this->publish_pending_ = true;
  }
  return true;
}

void PbHubSwitch::recovery_complete() {
  if (!this->publish_pending_)
    return;
  const bool state = this->pending_raw_;
  this->publish_pending_ = false;
  this->publish_state(state);
}
#endif

#ifdef USE_LIGHT
light::LightTraits PbHubRGBLight::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

bool PbHubRGBLight::capture_desired_state_(light::LightState *state) {
  if (state == nullptr)
    return false;
  float red;
  float green;
  float blue;
  state->current_values_as_rgb(&red, &green, &blue);
  if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) {
    if (!this->nonfinite_warning_logged_) {
      ESP_LOGW(TAG, "Ignored non-finite PBHUB RGB state");
      this->nonfinite_warning_logged_ = true;
    }
    return false;
  }

  const auto to_byte = [](float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
  };
  this->desired_color_ = {to_byte(red), to_byte(green), to_byte(blue)};
  this->desired_known_ = true;
  return true;
}

void PbHubRGBLight::update_state(light::LightState *state) { this->capture_desired_state_(state); }

void PbHubRGBLight::write_state(light::LightState *state) {
  if (this->parent_ == nullptr || !this->capture_desired_state_(state))
    return;

  if (this->desired_matches_applied_()) {
    this->write_pending_ = false;
    return;
  }
  this->write_pending_ = true;
  if (this->parent_->is_hub_ready())
    this->parent_->queue_rgb_write(this);
}

bool PbHubRGBLight::flush_pending_rgb_write() {
  if (!this->write_pending_ || this->parent_ == nullptr || !this->parent_->is_hub_ready())
    return false;
  if (this->desired_matches_applied_()) {
    this->write_pending_ = false;
    return false;
  }
  this->apply_desired_state_();
  return true;
}

bool PbHubRGBLight::restore_configuration() {
  if (this->configuration_applied_)
    return true;
  if (!this->parent_->configure_leds(this->channel_, this->led_count_) ||
      !this->parent_->set_led_full_brightness(this->channel_))
    return false;
  this->configuration_applied_ = true;
  return true;
}

bool PbHubRGBLight::replay_state() { return !this->desired_known_ || this->apply_desired_state_(); }

bool PbHubRGBLight::desired_matches_applied_() const {
  return this->applied_known_ && this->applied_color_.red == this->desired_color_.red &&
         this->applied_color_.green == this->desired_color_.green &&
         this->applied_color_.blue == this->desired_color_.blue;
}

bool PbHubRGBLight::apply_desired_state_() {
  if (this->desired_matches_applied_()) {
    this->write_pending_ = false;
    return true;
  }
  if (this->parent_ == nullptr ||
      !this->parent_->fill_leds(this->channel_, this->led_count_, 0, this->led_count_, this->desired_color_.red,
                                this->desired_color_.green, this->desired_color_.blue))
    return false;
  this->applied_color_ = this->desired_color_;
  this->applied_known_ = true;
  this->write_pending_ = false;
  return true;
}
#endif

}  // namespace esphome::pbhub
