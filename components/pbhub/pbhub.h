#pragma once

#include <cstddef>
#include <cstdint>

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

#include "pbhub_protocol.h"
#include "pbhub_ownership.h"
#include "pbhub_polling.h"
#include "pbhub_recovery.h"
#include "pbhub_rgb_queue.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_OUTPUT
#include "esphome/components/output/float_output.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_LIGHT
#include "esphome/components/light/light_output.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome::pbhub {

class PbHubComponent : public Component, public i2c::I2CDevice, private PbHubRecoveryBackend {
 public:
  static constexpr float SETUP_PRIORITY = setup_priority::IO;
  // Provisional host-side traffic policy. Real-hardware measurements must
  // validate or revise it; the firmware does not define a safe refresh rate.
  static constexpr uint32_t RGB_MIN_REFRESH_INTERVAL_US = 50'000;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return SETUP_PRIORITY; }

  bool is_hub_ready() const { return this->recovery_.is_ready(); }
  HubState get_hub_state() const { return this->recovery_.state(); }
  uint8_t get_firmware_version() const { return this->firmware_version_; }
  uint32_t get_consecutive_failures() const { return this->consecutive_failures_; }
  uint32_t get_total_failures() const { return this->total_failures_; }

  bool register_recovery_client(PbHubRecoveryClient *client);
  bool queue_poll(PbHubPollClient *client);
  bool queue_rgb_write(PbHubRGBWriteClient *client);
  bool claim_endpoint(uint8_t channel, uint8_t signal_index, EndpointOwner owner, const char *owner_id);
  void set_led_timing_mode(uint8_t mode);

  bool read_digital(protocol::Endpoint endpoint, bool &value);
  bool write_digital(protocol::Endpoint endpoint, bool value);
  bool read_adc(uint8_t channel, uint16_t &value);
  bool write_pwm(protocol::Endpoint endpoint, uint8_t duty);
  bool write_servo_pulse(protocol::Endpoint endpoint, uint16_t pulse_us);
  bool write_servo_detach(protocol::Endpoint endpoint);
  bool configure_leds(uint8_t channel, uint16_t count);
  bool set_led_full_brightness(uint8_t channel);
  bool fill_leds(uint8_t channel, uint16_t configured_count, uint16_t start, uint16_t count, uint8_t red,
                 uint8_t green, uint8_t blue);

 protected:
  FirmwareProbeResult probe_firmware() override;
  bool restore_global_configuration() override;

  bool feature_io_allowed_() const;
  bool endpoint_owned_by_(protocol::Endpoint endpoint, EndpointOwner owner, const char *operation) const;
  bool read_transaction_(const char *operation, const protocol::ReadCommand &command, uint8_t *data, size_t length);
  bool write_transaction_(const char *operation, uint8_t reg, const uint8_t *data, size_t length);

  template<size_t N> bool write_command_(const char *operation, const protocol::WriteCommand<N> &command) {
    if (!this->feature_io_allowed_())
      return false;
    return this->write_transaction_(operation, command.reg, command.payload.data(), N);
  }

  void handle_transport_failure_(const char *operation, uint8_t reg, i2c::ErrorCode error);
  void handle_protocol_failure_(const char *operation, uint8_t reg, const char *reason);
  void handle_transport_success_();
  void attempt_recovery_();
  void schedule_recovery_();
  bool write_led_timing_mode_(uint8_t mode);
  bool rgb_refresh_due_() const;

  PbHubRecoveryCoordinator recovery_;
  PbHubPollQueue poll_queue_;
  PbHubRGBWriteQueue rgb_write_queue_;
  EndpointClaimRegistry endpoint_claims_;
  bool configuration_error_{false};
  bool recovery_io_active_{false};

  uint8_t firmware_version_{0};
  bool ever_ready_{false};
  bool led_timing_configured_{false};
  uint8_t led_timing_mode_{0};

  uint32_t consecutive_failures_{0};
  uint32_t total_failures_{0};
  uint32_t last_failure_log_ms_{0};
  bool failure_logged_{false};
  bool recovery_scheduled_{false};
  bool prefer_poll_after_rgb_{false};
  bool rgb_refresh_started_{false};
  uint32_t last_rgb_refresh_us_{0};
};

#ifdef USE_BINARY_SENSOR
class PbHubBinarySensor : public binary_sensor::BinarySensor, public PollingComponent, public PbHubPollClient {
 public:
  PbHubBinarySensor(PbHubComponent *parent, uint8_t channel, uint8_t signal_index,
                    uint32_t update_interval = 100);

  void update() override;
  void perform_poll() override;
  void dump_config() override;
  void set_inverted(bool inverted) { this->inverted_ = inverted; }

 protected:
  PbHubComponent *parent_;
  protocol::Endpoint endpoint_{};
  bool endpoint_valid_{false};
  bool inverted_{false};
};
#endif

#ifdef USE_OUTPUT
class PbHubPWMOutput final : public output::FloatOutput, public Component, public PbHubRecoveryClient {
 public:
  PbHubPWMOutput(PbHubComponent *parent, uint8_t channel, uint8_t signal_index);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return PbHubComponent::SETUP_PRIORITY + 1.0f; }
  void update_frequency(float frequency) override;

  void invalidate_applied_state() override { this->applied_known_ = false; }
  bool restore_configuration() override { return true; }
  bool replay_state() override;

 protected:
  void write_state(float state) override;
  bool apply_desired_state_();

  PbHubComponent *parent_;
  protocol::Endpoint endpoint_{};
  uint8_t desired_duty_{0};
  uint8_t applied_duty_{0};
  protocol::PwmDriveMode desired_mode_{protocol::PwmDriveMode::DIGITAL_LOW};
  protocol::PwmDriveMode applied_mode_{protocol::PwmDriveMode::DIGITAL_LOW};
  bool endpoint_valid_{false};
  bool desired_known_{false};
  bool applied_known_{false};
  bool invalid_level_warning_logged_{false};
  bool frequency_warning_logged_{false};
};

class PbHubServoOutput final : public output::FloatOutput, public Component, public PbHubRecoveryClient {
 public:
  PbHubServoOutput(PbHubComponent *parent, uint8_t channel, uint8_t signal_index);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return PbHubComponent::SETUP_PRIORITY + 1.0f; }
  void update_frequency(float frequency) override;

  void invalidate_applied_state() override { this->applied_known_ = false; }
  bool restore_configuration() override { return true; }
  bool replay_state() override;

 protected:
  void write_state(float state) override;
  bool apply_desired_state_();

  PbHubComponent *parent_;
  protocol::Endpoint endpoint_{};
  uint16_t desired_pulse_us_{0};
  uint16_t applied_pulse_us_{0};
  bool endpoint_valid_{false};
  bool desired_known_{false};
  bool applied_known_{false};
  bool invalid_level_warning_logged_{false};
  bool invalid_transform_warning_logged_{false};
  bool nonfinite_warning_logged_{false};
  bool frequency_warning_logged_{false};
};
#endif

#ifdef USE_SENSOR
class PbHubADC : public sensor::Sensor, public PollingComponent, public PbHubPollClient {
 public:
  PbHubADC(PbHubComponent *parent, uint8_t channel, uint32_t update_interval = 1000);

  void update() override;
  void perform_poll() override;
  void dump_config() override;

 protected:
  PbHubComponent *parent_;
  uint8_t channel_;
};
#endif

#ifdef USE_SWITCH
class PbHubSwitch final : public switch_::Switch, public Component, public PbHubRecoveryClient {
 public:
  PbHubSwitch(PbHubComponent *parent, uint8_t channel, uint8_t signal_index);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return PbHubComponent::SETUP_PRIORITY + 1.0f; }

  void invalidate_applied_state() override {
    this->applied_known_ = false;
    this->publish_pending_ = false;
  }
  bool restore_configuration() override { return true; }
  bool replay_state() override;
  void recovery_complete() override;

 protected:
  void write_state(bool state) override;
  bool apply_desired_state_(bool publish_immediately);

  PbHubComponent *parent_;
  protocol::Endpoint endpoint_{};
  bool endpoint_valid_{false};
  bool desired_raw_{false};
  bool applied_raw_{false};
  bool pending_raw_{false};
  bool desired_known_{false};
  bool applied_known_{false};
  bool publish_pending_{false};
};
#endif

#ifdef USE_LIGHT
class PbHubRGBLight : public light::LightOutput, public PbHubRecoveryClient, public PbHubRGBWriteClient {
 public:
  PbHubRGBLight(PbHubComponent *parent, uint8_t channel) : parent_(parent), channel_(channel) {}

  light::LightTraits get_traits() override;
  void update_state(light::LightState *state) override;
  void write_state(light::LightState *state) override;
  bool flush_pending_rgb_write() override;

  void invalidate_applied_state() override {
    this->configuration_applied_ = false;
    this->applied_known_ = false;
    this->write_pending_ = this->desired_known_;
  }
  bool restore_configuration() override;
  bool replay_state() override;

  void set_led_count(uint16_t count) { this->led_count_ = count; }
  void set_startup_off(bool startup_off) {
    if (!startup_off)
      return;
    this->desired_color_ = {};
    this->desired_known_ = true;
    this->write_pending_ = true;
  }

 protected:
  bool capture_desired_state_(light::LightState *state);
  bool desired_matches_applied_() const;
  bool apply_desired_state_();

  PbHubComponent *parent_;
  uint8_t channel_;
  uint16_t led_count_{1};
  protocol::Rgb desired_color_{};
  protocol::Rgb applied_color_{};
  bool desired_known_{false};
  bool write_pending_{false};
  bool configuration_applied_{false};
  bool applied_known_{false};
  bool nonfinite_warning_logged_{false};
};
#endif

}  // namespace esphome::pbhub
