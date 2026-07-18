#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#define USE_LIGHT
#define USE_OUTPUT
#include "components/pbhub/pbhub.cpp"

using namespace esphome;
using namespace esphome::pbhub;

static int failures = 0;

#define CHECK(...)                                                                                                     \
  do {                                                                                                                 \
    if (!(__VA_ARGS__)) {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #__VA_ARGS__ << '\n';                           \
      failures++;                                                                                                      \
    }                                                                                                                  \
  } while (false)

class ScriptedI2CBus final : public i2c::I2CBus {
 public:
  struct Expectation {
    uint8_t address;
    std::vector<uint8_t> write;
    size_t read_count;
    i2c::ErrorCode result;
    std::vector<uint8_t> response;
  };

  void expect_read(uint8_t address, uint8_t reg, std::vector<uint8_t> response) {
    this->expectations_.push_back({address, {reg}, response.size(), i2c::ERROR_OK, std::move(response)});
  }

  void expect_read_failure(uint8_t address, uint8_t reg, size_t response_length, i2c::ErrorCode result) {
    this->expectations_.push_back({address, {reg}, response_length, result, {}});
  }

  void expect_write(uint8_t address, uint8_t reg, std::vector<uint8_t> payload) {
    payload.insert(payload.begin(), reg);
    this->expectations_.push_back({address, std::move(payload), 0, i2c::ERROR_OK, {}});
  }

  i2c::ErrorCode write_readv(uint8_t address, const uint8_t *write_buffer, size_t write_count, uint8_t *read_buffer,
                             size_t read_count) override {
    this->transaction_count_++;
    if (this->expectations_.empty()) {
      CHECK(false && "unexpected I2C transaction");
      return i2c::ERROR_UNKNOWN;
    }

    const auto expected = std::move(this->expectations_.front());
    this->expectations_.pop_front();
    CHECK(address == expected.address);
    CHECK(write_count == expected.write.size());
    CHECK(write_buffer != nullptr);
    if (write_buffer != nullptr && write_count == expected.write.size())
      CHECK(std::equal(expected.write.begin(), expected.write.end(), write_buffer));
    CHECK(read_count == expected.read_count);

    if (expected.result == i2c::ERROR_OK) {
      CHECK((read_count == 0) == (read_buffer == nullptr));
      CHECK(expected.response.size() == read_count);
      if (read_count != 0 && read_buffer != nullptr && expected.response.size() == read_count)
        std::copy(expected.response.begin(), expected.response.end(), read_buffer);
    }
    return expected.result;
  }

  size_t transaction_count() const { return this->transaction_count_; }
  bool empty() const { return this->expectations_.empty(); }

 private:
  std::deque<Expectation> expectations_;
  size_t transaction_count_{0};
};

static void test_supported_startup_and_recovery() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubPWMOutput pwm(&hub, 0, 1);
  PbHubRGBLight rgb(&hub, 1);
  rgb.set_led_count(12);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.get_setup_priority() == setup_priority::IO);
  CHECK(hub.claim_endpoint(0, 0, EndpointOwner::ADC, "test_adc"));
  CHECK(hub.claim_endpoint(0, 1, EndpointOwner::PWM, "test_pwm"));
  CHECK(hub.claim_endpoint(1, 0, EndpointOwner::SERVO, "test_servo"));
  CHECK(hub.claim_endpoint(1, 1, EndpointOwner::RGB, "test_rgb"));
  CHECK(hub.register_recovery_client(&pwm));
  CHECK(hub.register_recovery_client(&rgb));

  pwm.setup();
  CHECK(bus.transaction_count() == 0);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x58, {12, 0});
  bus.expect_write(0x61, 0x5B, {255});
  bus.expect_write(0x61, 0x41, {0});
  hub.setup();
  CHECK(hub.get_hub_state() == HubState::READY);
  CHECK(hub.is_hub_ready());
  CHECK(hub.get_firmware_version() == protocol::EXPECTED_FIRMWARE_VERSION);
  CHECK(!hub.status_has_warning());
  CHECK(!hub.is_failed());

  bus.expect_write(0x61, 0x50, {0});
  CHECK(hub.write_servo_detach({1, 0}));

  bus.expect_write(0x61, 0x43, {128});
  pwm.set_level(0.5f);
  light::LightState first_color(1.0f, 0.5f, 0.0f);
  bus.expect_write(0x61, 0x5A, {0, 0, 12, 0, 255, 128, 0});
  rgb.write_state(&first_color);
  hub.loop();

  uint16_t adc_value = 0xBEEF;
  bus.expect_read_failure(0x61, 0x46, 2, i2c::ERROR_NOT_ACKNOWLEDGED);
  CHECK(!hub.read_adc(0, adc_value));
  CHECK(adc_value == 0xBEEF);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(!hub.is_hub_ready());
  CHECK(hub.status_has_warning());
  CHECK(hub.get_consecutive_failures() == 1);
  CHECK(hub.get_total_failures() == 1);
  CHECK(hub.test_has_timeout("firmware_probe"));
  CHECK(hub.test_timeout_delay("firmware_probe") == 5000);

  const size_t transactions_before_suppressed_read = bus.transaction_count();
  adc_value = 0xCAFE;
  CHECK(!hub.read_adc(0, adc_value));
  CHECK(adc_value == 0xCAFE);
  CHECK(bus.transaction_count() == transactions_before_suppressed_read);

  pwm.set_level(0.25f);
  light::LightState recovered_color(0.0f, 0.25f, 1.0f);
  rgb.write_state(&recovered_color);
  CHECK(bus.transaction_count() == transactions_before_suppressed_read);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x58, {12, 0});
  bus.expect_write(0x61, 0x5B, {255});
  bus.expect_write(0x61, 0x43, {64});
  bus.expect_write(0x61, 0x5A, {0, 0, 12, 0, 0, 64, 255});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.get_hub_state() == HubState::READY);
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());
  CHECK(hub.get_consecutive_failures() == 0);
  CHECK(!hub.test_has_timeout("firmware_probe"));

  const size_t transactions_before_cached_outputs = bus.transaction_count();
  pwm.set_level(0.25f);
  rgb.write_state(&recovered_color);
  set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US);
  hub.loop();
  CHECK(bus.transaction_count() == transactions_before_cached_outputs);
  CHECK(bus.empty());
}

static void test_unsupported_firmware_is_terminal() {
  ScriptedI2CBus bus;
  PbHubComponent hub;

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.claim_endpoint(0, 0, EndpointOwner::ADC, "test_adc"));

  bus.expect_read_failure(0x61, protocol::REG_FIRMWARE_VERSION, 1, i2c::ERROR_TIMEOUT);
  hub.setup();
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());
  CHECK(hub.test_has_timeout("firmware_probe"));
  CHECK(hub.test_timeout_delay("firmware_probe") == 5000);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {3});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.get_hub_state() == HubState::UNSUPPORTED);
  CHECK(hub.get_firmware_version() == 3);
  CHECK(hub.is_failed());
  CHECK(hub.status_has_error());
  CHECK(!hub.status_has_warning());
  CHECK(!hub.test_has_timeout("firmware_probe"));

  const size_t transactions_before_suppressed_read = bus.transaction_count();
  uint16_t adc_value = 0x1234;
  CHECK(!hub.read_adc(0, adc_value));
  CHECK(adc_value == 0x1234);
  CHECK(bus.transaction_count() == transactions_before_suppressed_read);
  CHECK(bus.empty());
}

int main() {
  set_unit_test_millis(1000);
  test_supported_startup_and_recovery();
  test_unsupported_firmware_is_terminal();

  if (failures != 0)
    std::cerr << failures << " PBHUB component checks failed\n";
  return failures == 0 ? 0 : 1;
}
