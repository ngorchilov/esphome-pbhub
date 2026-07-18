#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <utility>
#include <vector>

#define USE_BINARY_SENSOR
#define USE_SENSOR
#define USE_SWITCH
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

class ControlledRecoveryClient final : public PbHubRecoveryClient {
 public:
  void invalidate_applied_state() override { this->invalidate_count_++; }
  bool restore_configuration() override { return true; }
  bool replay_state() override {
    this->replay_count_++;
    return !this->fail_replay_;
  }
  void recovery_complete() override { this->complete_count_++; }

  bool fail_replay_{true};
  size_t invalidate_count_{0};
  size_t replay_count_{0};
  size_t complete_count_{0};
};

static void ready_hub(PbHubComponent &hub, ScriptedI2CBus &bus) {
  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  hub.setup();
  CHECK(hub.is_hub_ready());
}

static void test_serialized_polling_and_inversion() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubBinarySensor normal(&hub, 0, 0);
  PbHubBinarySensor inverted(&hub, 0, 1);
  PbHubADC adc(&hub, 1);
  inverted.set_inverted(true);

  CHECK(hub.claim_endpoint(0, 0, EndpointOwner::DIGITAL_INPUT, "normal_input"));
  CHECK(hub.claim_endpoint(0, 1, EndpointOwner::DIGITAL_INPUT, "inverted_input"));
  CHECK(hub.claim_endpoint(1, 0, EndpointOwner::ADC, "adc"));
  ready_hub(hub, bus);

  CHECK(normal.get_update_interval() == 100);
  CHECK(inverted.get_update_interval() == 100);
  CHECK(adc.get_update_interval() == 1000);
  const size_t idle_transactions = bus.transaction_count();
  hub.loop();
  hub.loop();
  CHECK(bus.transaction_count() == idle_transactions);

  normal.update();
  inverted.update();
  normal.update();
  adc.update();

  bus.expect_read(0x61, 0x44, {1});
  hub.loop();
  CHECK(normal.has_state() && normal.state);
  CHECK(!inverted.has_state());
  CHECK(!adc.has_state());

  bus.expect_read(0x61, 0x45, {0});
  hub.loop();
  CHECK(inverted.has_state() && inverted.state);
  CHECK(!adc.has_state());

  bus.expect_read(0x61, 0x56, {0x34, 0x02});
  hub.loop();
  CHECK(adc.has_state() && adc.state == 564.0f);
  const size_t drained_transactions = bus.transaction_count();
  hub.loop();
  CHECK(bus.transaction_count() == drained_transactions);

  const size_t inverted_publications = inverted.test_publish_count();
  inverted.update();
  bus.expect_read_failure(0x61, 0x45, 1, i2c::ERROR_TIMEOUT);
  hub.loop();
  CHECK(inverted.state);
  CHECK(inverted.test_publish_count() == inverted_publications);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());

  normal.update();
  const size_t unverified_transactions = bus.transaction_count();
  hub.loop();
  CHECK(bus.transaction_count() == unverified_transactions);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());
  bus.expect_read(0x61, 0x44, {0});
  hub.loop();
  CHECK(normal.has_state() && !normal.state);
  CHECK(bus.empty());
}

static void test_initial_inverted_failure_stays_unknown() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubBinarySensor inverted(&hub, 0, 1);
  inverted.set_inverted(true);
  CHECK(hub.claim_endpoint(0, 1, EndpointOwner::DIGITAL_INPUT, "inverted_input"));
  ready_hub(hub, bus);

  inverted.update();
  bus.expect_read_failure(0x61, 0x45, 1, i2c::ERROR_NOT_ACKNOWLEDGED);
  hub.loop();
  CHECK(!inverted.has_state());
  CHECK(inverted.test_publish_count() == 0);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(bus.empty());
}

static void test_invalid_digital_response_preserves_unknown_and_last_state() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubBinarySensor input(&hub, 0, 0);
  CHECK(hub.claim_endpoint(0, 0, EndpointOwner::DIGITAL_INPUT, "input"));
  ready_hub(hub, bus);

  input.update();
  bus.expect_read(0x61, 0x44, {2});
  hub.loop();
  CHECK(!input.has_state());
  CHECK(input.test_publish_count() == 0);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());

  input.update();
  bus.expect_read(0x61, 0x44, {1});
  hub.loop();
  CHECK(input.has_state() && input.state);
  CHECK(input.test_publish_count() == 1);

  input.update();
  bus.expect_read(0x61, 0x44, {2});
  hub.loop();
  CHECK(input.state);
  CHECK(input.test_publish_count() == 1);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());
  CHECK(bus.empty());
}

static void test_switch_restore_inversion_failure_and_replay() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubSwitch normal(&hub, 2, 0);
  PbHubSwitch inverted(&hub, 2, 1);
  inverted.set_inverted(true);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(normal.get_setup_priority() > hub.get_setup_priority());
  CHECK(inverted.get_setup_priority() > hub.get_setup_priority());
  CHECK(hub.claim_endpoint(2, 0, EndpointOwner::DIGITAL_OUTPUT, "normal_switch"));
  CHECK(hub.claim_endpoint(2, 1, EndpointOwner::DIGITAL_OUTPUT, "inverted_switch"));
  CHECK(hub.register_recovery_client(&normal));
  CHECK(hub.register_recovery_client(&inverted));

  normal.setup();
  inverted.setup();
  CHECK(!normal.has_state());
  CHECK(!inverted.has_state());
  CHECK(bus.transaction_count() == 0);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x60, {0});
  bus.expect_write(0x61, 0x61, {1});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(normal.has_state() && !normal.state);
  CHECK(inverted.has_state() && !inverted.state);
  CHECK(normal.test_publish_count() == 1);
  CHECK(inverted.test_publish_count() == 1);

  bus.expect_write(0x61, 0x60, {1});
  normal.turn_on();
  CHECK(normal.state);
  CHECK(normal.test_publish_count() == 2);
  const size_t before_cached_write = bus.transaction_count();
  normal.turn_on();
  CHECK(bus.transaction_count() == before_cached_write);

  bus.expect_write(0x61, 0x61, {0});
  inverted.turn_on();
  CHECK(inverted.state);
  CHECK(inverted.test_publish_count() == 2);

  bus.expect_write_failure(0x61, 0x60, {0}, i2c::ERROR_NOT_ACKNOWLEDGED);
  normal.turn_off();
  CHECK(normal.state);
  CHECK(normal.test_publish_count() == 2);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);

  normal.turn_on();
  normal.turn_off();
  inverted.turn_off();
  const size_t offline_transactions = bus.transaction_count();
  CHECK(normal.state);
  CHECK(inverted.state);
  CHECK(bus.transaction_count() == offline_transactions);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x60, {0});
  bus.expect_write(0x61, 0x61, {1});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());
  CHECK(!normal.state);
  CHECK(!inverted.state);
  CHECK(normal.test_publish_count() == 3);
  CHECK(inverted.test_publish_count() == 3);
  CHECK(!hub.status_has_warning());
  CHECK(bus.empty());
}

static void test_disabled_switch_stays_unknown() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubSwitch disabled(&hub, 3, 0);
  disabled.set_restore_mode(switch_::SWITCH_RESTORE_DISABLED);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.claim_endpoint(3, 0, EndpointOwner::DIGITAL_OUTPUT, "disabled_switch"));
  CHECK(hub.register_recovery_client(&disabled));
  disabled.setup();
  CHECK(!disabled.has_state());

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(!disabled.has_state());
  CHECK(disabled.test_publish_count() == 0);
  CHECK(bus.empty());
}

static void test_persisted_and_inverted_restore_modes() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubSwitch restored_on(&hub, 0, 0);
  PbHubSwitch inverted_restored_on(&hub, 0, 1);
  PbHubSwitch restored_inverted_off(&hub, 1, 0);
  PbHubSwitch inverted_restored_inverted_off(&hub, 1, 1);

  restored_on.set_restore_mode(switch_::SWITCH_RESTORE_DEFAULT_OFF);
  restored_on.test_set_restored_state(true);
  inverted_restored_on.set_inverted(true);
  inverted_restored_on.set_restore_mode(switch_::SWITCH_RESTORE_DEFAULT_OFF);
  inverted_restored_on.test_set_restored_state(true);
  restored_inverted_off.set_restore_mode(switch_::SWITCH_RESTORE_INVERTED_DEFAULT_OFF);
  restored_inverted_off.test_set_restored_state(true);
  inverted_restored_inverted_off.set_inverted(true);
  inverted_restored_inverted_off.set_restore_mode(switch_::SWITCH_RESTORE_INVERTED_DEFAULT_OFF);
  inverted_restored_inverted_off.test_set_restored_state(true);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.claim_endpoint(0, 0, EndpointOwner::DIGITAL_OUTPUT, "restored_on"));
  CHECK(hub.claim_endpoint(0, 1, EndpointOwner::DIGITAL_OUTPUT, "inverted_restored_on"));
  CHECK(hub.claim_endpoint(1, 0, EndpointOwner::DIGITAL_OUTPUT, "restored_inverted_off"));
  CHECK(hub.claim_endpoint(1, 1, EndpointOwner::DIGITAL_OUTPUT, "inverted_restored_inverted_off"));
  CHECK(hub.register_recovery_client(&restored_on));
  CHECK(hub.register_recovery_client(&inverted_restored_on));
  CHECK(hub.register_recovery_client(&restored_inverted_off));
  CHECK(hub.register_recovery_client(&inverted_restored_inverted_off));

  restored_on.setup();
  inverted_restored_on.setup();
  restored_inverted_off.setup();
  inverted_restored_inverted_off.setup();
  CHECK(bus.transaction_count() == 0);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x40, {1});
  bus.expect_write(0x61, 0x41, {0});
  bus.expect_write(0x61, 0x50, {0});
  bus.expect_write(0x61, 0x51, {1});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(restored_on.has_state() && restored_on.state);
  CHECK(inverted_restored_on.has_state() && inverted_restored_on.state);
  CHECK(restored_inverted_off.has_state() && !restored_inverted_off.state);
  CHECK(inverted_restored_inverted_off.has_state() && !inverted_restored_inverted_off.state);
  CHECK(bus.empty());
}

static void test_failed_switch_request_replays_without_intervening_command() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubSwitch output(&hub, 2, 0);
  output.set_restore_mode(switch_::SWITCH_RESTORE_DISABLED);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.claim_endpoint(2, 0, EndpointOwner::DIGITAL_OUTPUT, "output"));
  CHECK(hub.register_recovery_client(&output));
  output.setup();

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(!output.has_state());

  bus.expect_write_failure(0x61, 0x60, {1}, i2c::ERROR_NOT_ACKNOWLEDGED);
  output.turn_on();
  CHECK(!output.has_state());
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x60, {1});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());
  CHECK(output.has_state() && output.state);
  CHECK(output.test_publish_count() == 1);
  CHECK(bus.empty());
}

static void test_partial_recovery_does_not_publish_switch() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubSwitch output(&hub, 3, 0);
  ControlledRecoveryClient later_client;

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.claim_endpoint(3, 0, EndpointOwner::DIGITAL_OUTPUT, "output"));
  CHECK(hub.register_recovery_client(&output));
  CHECK(hub.register_recovery_client(&later_client));
  output.setup();

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x70, {0});
  hub.setup();
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(!output.has_state());
  CHECK(output.test_publish_count() == 0);
  CHECK(later_client.replay_count_ == 1);
  CHECK(later_client.complete_count_ == 0);

  later_client.fail_replay_ = false;
  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x70, {0});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());
  CHECK(output.has_state() && !output.state);
  CHECK(output.test_publish_count() == 1);
  CHECK(later_client.replay_count_ == 2);
  CHECK(later_client.complete_count_ == 1);
  CHECK(bus.empty());
}

int main() {
  set_unit_test_millis(1000);
  test_serialized_polling_and_inversion();
  test_initial_inverted_failure_stays_unknown();
  test_invalid_digital_response_preserves_unknown_and_last_state();
  test_switch_restore_inversion_failure_and_replay();
  test_disabled_switch_stays_unknown();
  test_persisted_and_inverted_restore_modes();
  test_failed_switch_request_replays_without_intervening_command();
  test_partial_recovery_does_not_publish_switch();

  if (failures != 0)
    std::cerr << failures << " digital-entity checks failed\n";
  return failures == 0 ? 0 : 1;
}
