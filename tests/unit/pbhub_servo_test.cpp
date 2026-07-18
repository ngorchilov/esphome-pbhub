#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#define USE_OUTPUT
#define USE_OUTPUT_FLOAT_POWER_SCALING
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

static void setup_ready_servo(PbHubComponent &hub, PbHubServoOutput &servo, ScriptedI2CBus &bus, uint8_t endpoint,
                              uint8_t detach_register) {
  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  servo.set_zero_means_zero(true);
  CHECK(servo.get_setup_priority() > hub.get_setup_priority());
  CHECK(hub.claim_endpoint(endpoint, EndpointOwner::SERVO, "servo"));
  CHECK(hub.register_recovery_client(&servo));

  servo.setup();
  CHECK(bus.transaction_count() == 0);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, detach_register, {0});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());
  CHECK(bus.empty());
}

static void test_startup_staging_and_calibrated_pulses_on_a_and_b() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubServoOutput signal_a(&hub, 20);
  PbHubServoOutput signal_b(&hub, 21);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  signal_a.set_zero_means_zero(true);
  signal_b.set_zero_means_zero(true);
  CHECK(signal_a.get_setup_priority() > hub.get_setup_priority());
  CHECK(signal_b.get_setup_priority() > hub.get_setup_priority());
  CHECK(hub.claim_endpoint(20, EndpointOwner::SERVO, "servo_a"));
  CHECK(hub.claim_endpoint(21, EndpointOwner::SERVO, "servo_b"));
  CHECK(hub.register_recovery_client(&signal_a));
  CHECK(hub.register_recovery_client(&signal_b));

  signal_a.setup();
  signal_b.setup();
  CHECK(bus.transaction_count() == 0);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x60, {0});
  bus.expect_write(0x61, 0x61, {0});
  hub.setup();
  CHECK(hub.is_hub_ready());

  bus.expect_write(0x61, 0x6E, {0xF4, 0x01});
  signal_a.set_level(0.025f);
  bus.expect_write(0x61, 0x6E, {0xDC, 0x05});
  signal_a.set_level(0.075f);
  bus.expect_write(0x61, 0x6E, {0xC4, 0x09});
  signal_a.set_level(0.125f);

  bus.expect_write(0x61, 0x6F, {0x58, 0x02});
  signal_b.set_level(0.03f);
  bus.expect_write(0x61, 0x6F, {0xDC, 0x05});
  signal_b.set_level(0.075f);
  bus.expect_write(0x61, 0x6F, {0x60, 0x09});
  signal_b.set_level(0.12f);

  const size_t after_commands = bus.transaction_count();
  signal_b.set_level(0.12f);
  CHECK(bus.transaction_count() == after_commands);
  CHECK(bus.empty());
}

static void test_preprobe_desired_pulse_is_preserved() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubServoOutput servo(&hub, 11);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  servo.set_zero_means_zero(true);
  CHECK(hub.claim_endpoint(11, EndpointOwner::SERVO, "preseeded_servo"));
  CHECK(hub.register_recovery_client(&servo));

  servo.set_level(0.075f);
  servo.setup();
  CHECK(bus.transaction_count() == 0);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x5F, {0xDC, 0x05});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(bus.empty());
}

static void test_rounding_dedup_and_detach_transitions() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubServoOutput servo(&hub, 21);
  setup_ready_servo(hub, servo, bus, 21, 0x61);

  bus.expect_write(0x61, 0x6F, {0xF4, 0x01});
  servo.set_level(0.024999f);
  const size_t after_500 = bus.transaction_count();
  servo.set_level(0.025024f);
  CHECK(bus.transaction_count() == after_500);

  bus.expect_write(0x61, 0x6F, {0xC4, 0x09});
  servo.set_level(0.125024f);
  const size_t after_2500 = bus.transaction_count();
  servo.set_level(0.125f);
  CHECK(bus.transaction_count() == after_2500);

  bus.expect_write(0x61, 0x61, {0});
  servo.set_level(0.0f);
  const size_t after_detach = bus.transaction_count();
  servo.set_level(0.0f);
  CHECK(bus.transaction_count() == after_detach);

  bus.expect_write(0x61, 0x6F, {0xDC, 0x05});
  servo.set_level(0.075f);
  bus.expect_write(0x61, 0x61, {0});
  servo.turn_off();
  CHECK(bus.empty());
}

static void test_invalid_and_nonfinite_levels_preserve_state() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubServoOutput servo(&hub, 1);
  setup_ready_servo(hub, servo, bus, 1, 0x41);
  unit_test_warning_count = 0;

  bus.expect_write(0x61, 0x4F, {0xDC, 0x05});
  servo.set_level(0.075f);
  const size_t after_center = bus.transaction_count();

  CHECK(!hub.write_servo_pulse({0, 1}, 499));
  CHECK(!hub.write_servo_pulse({0, 1}, 2501));
  servo.test_write_state(-0.01f);
  servo.test_write_state(0.0249f);
  servo.test_write_state(0.1251f);
  servo.turn_on();
  CHECK(unit_test_warning_count == 1);
  CHECK(bus.transaction_count() == after_center);
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());

  servo.invalidate_applied_state();
  bus.expect_write(0x61, 0x4F, {0xDC, 0x05});
  CHECK(servo.replay_state());
  const size_t after_replay = bus.transaction_count();

  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();
  servo.set_level(nan);
  servo.test_write_state(infinity);
  servo.test_write_state(-infinity);
  CHECK(unit_test_warning_count == 2);
  CHECK(bus.transaction_count() == after_replay);
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());

  servo.set_level(infinity);
  CHECK(bus.transaction_count() == after_replay);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_replay);

  bus.expect_write(0x61, 0x41, {0});
  servo.set_level(-infinity);
  const size_t after_negative_infinity = bus.transaction_count();
  servo.set_level(0.0f);
  CHECK(bus.transaction_count() == after_negative_infinity);
  CHECK(bus.empty());
}

static void test_runtime_transform_defense_and_zero_detach() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubServoOutput servo(&hub, 1);
  setup_ready_servo(hub, servo, bus, 1, 0x41);
  unit_test_warning_count = 0;

  bus.expect_write(0x61, 0x4F, {0xDC, 0x05});
  servo.set_level(0.075f);
  const size_t after_center = bus.transaction_count();

  servo.set_inverted(true);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_center);
  servo.set_inverted(false);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_center);

  servo.set_min_power(0.1f);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_center);
  servo.set_min_power(0.0f);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_center);

  servo.set_max_power(0.9f);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_center);
  servo.set_max_power(1.0f);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_center);

  servo.set_zero_means_zero(false);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_center);
  servo.set_zero_means_zero(true);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == after_center);
  CHECK(unit_test_warning_count == 1);
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());

  servo.set_min_power(0.1f);
  bus.expect_write(0x61, 0x41, {0});
  servo.set_level(0.0f);
  const size_t after_detach = bus.transaction_count();
  servo.set_level(0.0f);
  CHECK(bus.transaction_count() == after_detach);
  servo.set_min_power(0.0f);
  CHECK(bus.empty());
}

static void test_frequency_is_a_warning_only_noop() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubServoOutput servo(&hub, 1);
  setup_ready_servo(hub, servo, bus, 1, 0x41);
  unit_test_warning_count = 0;

  bus.expect_write(0x61, 0x4F, {0xDC, 0x05});
  servo.set_level(0.075f);
  const size_t before_frequency_requests = bus.transaction_count();
  servo.update_frequency(60.0f);
  CHECK(unit_test_warning_count == 1);
  CHECK(bus.transaction_count() == before_frequency_requests);
  servo.update_frequency(100.0f);
  CHECK(unit_test_warning_count == 1);
  CHECK(bus.transaction_count() == before_frequency_requests);
  servo.set_level(0.075f);
  CHECK(bus.transaction_count() == before_frequency_requests);
  CHECK(bus.empty());
}

static void test_failed_write_offline_last_valid_and_recovery() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubServoOutput servo(&hub, 11);
  setup_ready_servo(hub, servo, bus, 11, 0x51);

  bus.expect_write(0x61, 0x5F, {0xDC, 0x05});
  servo.set_level(0.075f);
  bus.expect_write_failure(0x61, 0x5F, {0xD0, 0x07}, i2c::ERROR_NOT_ACKNOWLEDGED);
  servo.set_level(0.1f);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());

  const size_t after_failure = bus.transaction_count();
  servo.set_level(0.12f);
  servo.test_write_state(0.13f);
  CHECK(bus.transaction_count() == after_failure);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x5F, {0x60, 0x09});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());

  const size_t after_recovery = bus.transaction_count();
  servo.set_level(0.12f);
  CHECK(bus.transaction_count() == after_recovery);
  bus.expect_write(0x61, 0x5F, {0xF4, 0x01});
  servo.set_level(0.025f);
  CHECK(bus.empty());
}

static void test_failed_detach_replays_detach_after_recovery() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubServoOutput servo(&hub, 30);
  setup_ready_servo(hub, servo, bus, 30, 0x70);

  bus.expect_write(0x61, 0x7E, {0xDC, 0x05});
  servo.set_level(0.075f);
  bus.expect_write_failure(0x61, 0x70, {0}, i2c::ERROR_TIMEOUT);
  servo.set_level(0.0f);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());

  const size_t after_failure = bus.transaction_count();
  servo.test_write_state(0.2f);
  CHECK(bus.transaction_count() == after_failure);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x70, {0});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());

  const size_t after_recovery = bus.transaction_count();
  servo.set_level(0.0f);
  CHECK(bus.transaction_count() == after_recovery);
  bus.expect_write(0x61, 0x7E, {0xF4, 0x01});
  servo.set_level(0.025f);
  CHECK(bus.empty());
}

int main() {
  set_unit_test_millis(1000);
  test_startup_staging_and_calibrated_pulses_on_a_and_b();
  test_preprobe_desired_pulse_is_preserved();
  test_rounding_dedup_and_detach_transitions();
  test_invalid_and_nonfinite_levels_preserve_state();
  test_runtime_transform_defense_and_zero_detach();
  test_frequency_is_a_warning_only_noop();
  test_failed_write_offline_last_valid_and_recovery();
  test_failed_detach_replays_detach_after_recovery();

  if (failures != 0)
    std::cerr << failures << " PBHUB servo checks failed\n";
  return failures == 0 ? 0 : 1;
}
