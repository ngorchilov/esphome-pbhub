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

void PbHubComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "PBHUB:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  State: %s", hub_state_name(this->recovery_.state()));
  if (this->firmware_version_ != 0)
    ESP_LOGCONFIG(TAG, "  Firmware protocol version: %u", this->firmware_version_);
  else
    ESP_LOGCONFIG(TAG, "  Firmware protocol version: unverified");
  ESP_LOGCONFIG(TAG, "  Recovery clients: %u", static_cast<unsigned>(this->recovery_.client_count()));
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

bool PbHubComponent::claim_endpoint(uint8_t encoded_endpoint, EndpointOwner owner, const char *owner_id) {
  protocol::Endpoint endpoint{};
  if (!protocol::decode_endpoint(encoded_endpoint, endpoint) || !is_valid_endpoint_owner(owner) || owner_id == nullptr) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB endpoint claim for encoded endpoint %u", encoded_endpoint);
    this->configuration_error_ = true;
    return false;
  }

  const auto *existing = this->endpoint_claims_.find(endpoint);
  if (existing == nullptr) {
    if (!this->endpoint_claims_.claim(endpoint, owner, owner_id)) {
      ESP_LOGE(TAG, "Rejected invalid PBHUB endpoint claim for encoded endpoint %u", encoded_endpoint);
      this->configuration_error_ = true;
      return false;
    }
    return true;
  }

  ESP_LOGE(TAG, "PBHUB endpoint %u is claimed by both %s '%s' and %s '%s'", encoded_endpoint,
           endpoint_owner_name(existing->owner), existing->owner_id, endpoint_owner_name(owner), owner_id);
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
    this->status_clear_warning();
    this->failure_logged_ = false;
    if (this->ever_ready_)
      ESP_LOGI(TAG, "PBHUB communication recovered; firmware protocol version %u verified", this->firmware_version_);
    else
      ESP_LOGCONFIG(TAG, "  Firmware protocol version: %u", this->firmware_version_);
    this->ever_ready_ = true;
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
  if (!this->endpoint_owned_by_({channel, 0}, EndpointOwner::ADC, "ADC read") || !this->feature_io_allowed_())
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
  if (!protocol::make_digital_write(endpoint, false, command)) {
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
    ESP_LOGE(TAG, "Rejected invalid PBHUB LED channel or count %u", count);
    return false;
  }
  if (!this->endpoint_owned_by_({channel, 1}, EndpointOwner::RGB, "LED count write"))
    return false;
  return this->write_command_("LED count write", command);
}

bool PbHubComponent::set_led_full_brightness(uint8_t channel) {
  protocol::WriteCommand<1> command{};
  if (!protocol::make_led_full_brightness_write(channel, command)) {
    ESP_LOGE(TAG, "Rejected invalid PBHUB LED channel %u", channel);
    return false;
  }
  if (!this->endpoint_owned_by_({channel, 1}, EndpointOwner::RGB, "LED brightness write"))
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
  if (!this->endpoint_owned_by_({channel, 1}, EndpointOwner::RGB, "LED fill write"))
    return false;
  return this->write_command_("LED fill write", command);
}

#ifdef USE_OUTPUT
void PbHubPWMPin::write_state(float state) {
  if (this->parent_ == nullptr)
    return;
  const float clamped = std::max(0.0f, std::min(1.0f, state));
  this->desired_duty_ = static_cast<uint8_t>(std::lround(clamped * 255.0f));
  this->desired_known_ = true;
  if (this->parent_->is_hub_ready())
    this->apply_desired_state_();
}

bool PbHubPWMPin::replay_state() { return !this->desired_known_ || this->apply_desired_state_(); }

bool PbHubPWMPin::apply_desired_state_() {
  if (this->applied_known_ && this->applied_duty_ == this->desired_duty_)
    return true;
  protocol::Endpoint endpoint{};
  if (!protocol::decode_endpoint(this->pin_, endpoint) || !this->parent_->write_pwm(endpoint, this->desired_duty_))
    return false;
  this->applied_duty_ = this->desired_duty_;
  this->applied_known_ = true;
  return true;
}
#endif

#ifdef USE_SENSOR
PbHubADC::PbHubADC(PbHubComponent *parent, uint8_t slot, uint32_t update_interval)
    : PollingComponent(update_interval), parent_(parent), slot_(slot) {}

void PbHubADC::update() {
  if (this->parent_ == nullptr)
    return;
  uint16_t value;
  if (this->parent_->read_adc(this->slot_, value))
    this->publish_state(value);
}
#endif

#ifdef USE_LIGHT
light::LightTraits PbHubRGBLight::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

void PbHubRGBLight::write_state(light::LightState *state) {
  if (this->parent_ == nullptr)
    return;

  float red;
  float green;
  float blue;
  state->current_values_as_rgb(&red, &green, &blue);
  const auto to_byte = [](float value) {
    return static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(1.0f, value)) * 255.0f));
  };
  this->desired_color_ = {to_byte(red), to_byte(green), to_byte(blue)};
  this->desired_known_ = true;
  if (this->parent_->is_hub_ready())
    this->apply_desired_state_();
}

bool PbHubRGBLight::restore_configuration() {
  if (this->configuration_applied_)
    return true;
  if (!this->parent_->configure_leds(this->slot_, this->led_count_) ||
      !this->parent_->set_led_full_brightness(this->slot_))
    return false;
  this->configuration_applied_ = true;
  return true;
}

bool PbHubRGBLight::replay_state() { return !this->desired_known_ || this->apply_desired_state_(); }

bool PbHubRGBLight::apply_desired_state_() {
  if (this->applied_known_ && this->applied_color_.red == this->desired_color_.red &&
      this->applied_color_.green == this->desired_color_.green &&
      this->applied_color_.blue == this->desired_color_.blue)
    return true;
  if (!this->parent_->fill_leds(this->slot_, this->led_count_, 0, this->led_count_, this->desired_color_.red,
                                this->desired_color_.green, this->desired_color_.blue))
    return false;
  this->applied_color_ = this->desired_color_;
  this->applied_known_ = true;
  return true;
}
#endif

}  // namespace esphome::pbhub
