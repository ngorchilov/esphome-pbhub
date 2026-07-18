#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#define USE_OUTPUT
#define USE_SENSOR
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

  void expect_write(uint8_t address, uint8_t reg, std::vector<uint8_t> payload) {
    this->expect_write_result_(address, reg, std::move(payload), i2c::ERROR_OK);
  }

  void expect_write_failure(uint8_t address, uint8_t reg, std::vector<uint8_t> payload, i2c::ErrorCode result) {
    this->expect_write_result_(address, reg, std::move(payload), result);
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

 protected:
  void expect_write_result_(uint8_t address, uint8_t reg, std::vector<uint8_t> payload, i2c::ErrorCode result) {
    payload.insert(payload.begin(), reg);
    this->expectations_.push_back({address, std::move(payload), 0, result, {}});
  }

  std::deque<Expectation> expectations_;
  size_t transaction_count_{0};
};

static void setup_ready_pwm(PbHubComponent &hub, PbHubPWMOutput &pwm, ScriptedI2CBus &bus, uint8_t endpoint,
                            uint8_t digital_register) {
  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(pwm.get_setup_priority() > hub.get_setup_priority());
  CHECK(hub.claim_endpoint(endpoint, EndpointOwner::PWM, "pwm"));
  CHECK(hub.register_recovery_client(&pwm));

  pwm.setup();
  CHECK(bus.transaction_count() == 0);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, digital_register, {0});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());
  CHECK(bus.empty());
}

static void test_startup_extrema_intermediate_levels_and_cache() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubPWMOutput pwm(&hub, 21);
  setup_ready_pwm(hub, pwm, bus, 21, 0x61);

  const size_t after_startup = bus.transaction_count();
  pwm.set_level(0.0f);
  CHECK(bus.transaction_count() == after_startup);

  bus.expect_write(0x61, 0x63, {1});
  pwm.set_level(1.0f / 255.0f);
  bus.expect_write(0x61, 0x63, {127});
  pwm.set_level(127.0f / 255.0f);
  bus.expect_write(0x61, 0x63, {128});
  pwm.set_level(128.0f / 255.0f);

  const size_t after_128 = bus.transaction_count();
  pwm.set_level(0.5001f);
  CHECK(bus.transaction_count() == after_128);

  bus.expect_write(0x61, 0x63, {254});
  pwm.set_level(254.0f / 255.0f);
  bus.expect_write(0x61, 0x61, {1});
  pwm.set_level(1.0f);

  const size_t after_255 = bus.transaction_count();
  pwm.set_level(1.0f);
  CHECK(bus.transaction_count() == after_255);

  bus.expect_write(0x61, 0x63, {254});
  pwm.set_level(254.0f / 255.0f);
  bus.expect_write(0x61, 0x61, {1});
  pwm.set_level(1.0f);
  bus.expect_write(0x61, 0x61, {0});
  pwm.set_level(0.0f);
  bus.expect_write(0x61, 0x63, {1});
  pwm.set_level(1.0f / 255.0f);
  bus.expect_write(0x61, 0x61, {0});
  pwm.set_level(0.0f);
  CHECK(bus.empty());
}

static void test_startup_transforms_and_preexisting_desired_level() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubPWMOutput inverted(&hub, 0);
  PbHubPWMOutput preseeded(&hub, 1);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.claim_endpoint(0, EndpointOwner::PWM, "inverted"));
  CHECK(hub.claim_endpoint(1, EndpointOwner::PWM, "preseeded"));
  CHECK(hub.register_recovery_client(&inverted));
  CHECK(hub.register_recovery_client(&preseeded));

  inverted.set_inverted(true);
  preseeded.set_level(0.5f);
  inverted.setup();
  preseeded.setup();
  CHECK(bus.transaction_count() == 0);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x40, {1});
  bus.expect_write(0x61, 0x43, {128});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(bus.empty());
}

static void test_inversion_and_power_transforms() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubPWMOutput pwm(&hub, 1);
  setup_ready_pwm(hub, pwm, bus, 1, 0x41);

  pwm.set_min_power(0.2f);
  pwm.set_max_power(0.8f);

  bus.expect_write(0x61, 0x43, {51});
  pwm.set_level(0.0f);
  bus.expect_write(0x61, 0x43, {128});
  pwm.set_level(0.5f);

  pwm.set_zero_means_zero(true);
  bus.expect_write(0x61, 0x41, {0});
  pwm.set_level(0.0f);

  pwm.set_inverted(true);
  bus.expect_write(0x61, 0x41, {1});
  pwm.set_level(0.0f);
  bus.expect_write(0x61, 0x43, {51});
  pwm.set_level(1.0f);
  CHECK(bus.empty());
}

static void test_failed_write_offline_last_desired_and_recovery() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubPWMOutput pwm(&hub, 1);
  setup_ready_pwm(hub, pwm, bus, 1, 0x41);

  bus.expect_write(0x61, 0x43, {64});
  pwm.set_level(0.25f);

  bus.expect_write_failure(0x61, 0x43, {128}, i2c::ERROR_NOT_ACKNOWLEDGED);
  pwm.set_level(0.5f);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());

  const size_t after_failure = bus.transaction_count();
  pwm.set_level(0.75f);
  pwm.set_level(1.0f);
  CHECK(bus.transaction_count() == after_failure);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x41, {1});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());

  const size_t after_recovery = bus.transaction_count();
  pwm.set_level(1.0f);
  CHECK(bus.transaction_count() == after_recovery);
  bus.expect_write(0x61, 0x43, {254});
  pwm.set_level(254.0f / 255.0f);
  CHECK(bus.empty());
}

static void test_frequency_nonfinite_and_infinite_levels() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubPWMOutput pwm(&hub, 1);
  setup_ready_pwm(hub, pwm, bus, 1, 0x41);
  unit_test_warning_count = 0;

  bus.expect_write(0x61, 0x43, {128});
  pwm.set_level(0.5f);
  const size_t after_half = bus.transaction_count();

  pwm.update_frequency(1000.0f);
  CHECK(unit_test_warning_count == 1);
  CHECK(bus.transaction_count() == after_half);
  pwm.update_frequency(2000.0f);
  CHECK(unit_test_warning_count == 1);
  CHECK(bus.transaction_count() == after_half);
  pwm.set_level(0.5f);
  CHECK(bus.transaction_count() == after_half);

  const float nan = std::numeric_limits<float>::quiet_NaN();
  pwm.set_level(nan);
  CHECK(unit_test_warning_count == 2);
  CHECK(bus.transaction_count() == after_half);
  pwm.set_level(nan);
  CHECK(unit_test_warning_count == 2);
  CHECK(bus.transaction_count() == after_half);
  pwm.set_level(0.5f);
  CHECK(bus.transaction_count() == after_half);

  bus.expect_write(0x61, 0x41, {1});
  pwm.set_level(std::numeric_limits<float>::infinity());
  bus.expect_write(0x61, 0x41, {0});
  pwm.set_level(-std::numeric_limits<float>::infinity());
  CHECK(bus.empty());
}

static void test_same_slot_adc_a_and_pwm_b_are_independent() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubPWMOutput pwm(&hub, 21);
  PbHubADC adc(&hub, 2);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.claim_endpoint(21, EndpointOwner::PWM, "pwm_b"));
  CHECK(hub.claim_endpoint(20, EndpointOwner::ADC, "adc_a"));
  CHECK(hub.register_recovery_client(&pwm));
  pwm.setup();

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x61, {0});
  hub.setup();
  CHECK(hub.is_hub_ready());

  adc.update();
  bus.expect_read(0x61, 0x66, {0x34, 0x02});
  hub.loop();
  CHECK(adc.has_state());
  CHECK(adc.state == 564.0f);

  bus.expect_write(0x61, 0x63, {128});
  pwm.set_level(0.5f);
  const size_t after_pwm = bus.transaction_count();
  pwm.set_level(0.5001f);
  CHECK(bus.transaction_count() == after_pwm);

  adc.update();
  bus.expect_read(0x61, 0x66, {0x00, 0x00});
  hub.loop();
  CHECK(adc.state == 0.0f);
  CHECK(adc.test_publish_count() == 2);
  const size_t after_adc = bus.transaction_count();
  pwm.set_level(0.5001f);
  CHECK(bus.transaction_count() == after_adc);
  CHECK(bus.empty());
}

int main() {
  set_unit_test_millis(1000);
  test_startup_extrema_intermediate_levels_and_cache();
  test_startup_transforms_and_preexisting_desired_level();
  test_inversion_and_power_transforms();
  test_failed_write_offline_last_desired_and_recovery();
  test_frequency_nonfinite_and_infinite_levels();
  test_same_slot_adc_a_and_pwm_b_are_independent();

  if (failures != 0)
    std::cerr << failures << " PBHUB PWM checks failed\n";
  return failures == 0 ? 0 : 1;
}
