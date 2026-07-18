#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <utility>
#include <vector>

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

  void expect_read_failure(uint8_t address, uint8_t reg, size_t response_length, i2c::ErrorCode result) {
    this->expectations_.push_back({address, {reg}, response_length, result, {}});
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
      CHECK(read_buffer != nullptr);
      CHECK(expected.response.size() == read_count);
      if (read_buffer != nullptr && expected.response.size() == read_count)
        std::copy(expected.response.begin(), expected.response.end(), read_buffer);
    }
    return expected.result;
  }

  size_t transaction_count() const { return this->transaction_count_; }
  bool empty() const { return this->expectations_.empty(); }

 protected:
  std::deque<Expectation> expectations_;
  size_t transaction_count_{0};
};

static void ready_hub(PbHubComponent &hub, ScriptedI2CBus &bus) {
  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  hub.setup();
  CHECK(hub.is_hub_ready());
}

static void test_all_channels_are_serialized_and_little_endian() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  std::array<PbHubADC, protocol::CHANNEL_COUNT> sensors{{
      {&hub, 0, 250}, {&hub, 1}, {&hub, 2}, {&hub, 3}, {&hub, 4}, {&hub, 5},
  }};
  const std::array<uint16_t, protocol::CHANNEL_COUNT> values{{0, 1, 0x0123, 0x0800, 0x0FFE, 0x0FFF}};

  for (uint8_t channel = 0; channel < protocol::CHANNEL_COUNT; channel++)
    CHECK(hub.claim_endpoint(channel, 0, EndpointOwner::ADC, "adc"));
  ready_hub(hub, bus);
  CHECK(sensors[0].get_update_interval() == 250);
  CHECK(sensors[1].get_update_interval() == 1000);

  for (auto &sensor : sensors)
    sensor.update();

  const size_t before_reads = bus.transaction_count();
  for (uint8_t channel = 0; channel < protocol::CHANNEL_COUNT; channel++) {
    const uint16_t value = values[channel];
    bus.expect_read(0x61, protocol::CHANNEL_BASES[channel] + 0x06,
                    {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)});
    hub.loop();
    CHECK(bus.transaction_count() == before_reads + channel + 1);
    CHECK(sensors[channel].has_state());
    CHECK(sensors[channel].state == static_cast<float>(value));
    CHECK(sensors[channel].test_publish_count() == 1);
    if (channel + 1 < protocol::CHANNEL_COUNT)
      CHECK(!sensors[channel + 1].has_state());
  }

  sensors[0].update();
  bus.expect_read_failure(0x61, 0x46, 2, i2c::ERROR_TIMEOUT);
  hub.loop();
  CHECK(sensors[0].state == 0.0f);
  CHECK(sensors[0].test_publish_count() == 1);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());

  sensors[5].update();
  bus.expect_read(0x61, 0xA6, {0x00, 0x10});
  hub.loop();
  CHECK(sensors[5].state == 4095.0f);
  CHECK(sensors[5].test_publish_count() == 1);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());
  CHECK(bus.empty());
}

static void test_initial_failure_stays_unknown() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubADC sensor(&hub, 0);
  CHECK(hub.claim_endpoint(0, 0, EndpointOwner::ADC, "adc"));
  ready_hub(hub, bus);

  sensor.update();
  bus.expect_read_failure(0x61, 0x46, 2, i2c::ERROR_NOT_ACKNOWLEDGED);
  hub.loop();
  CHECK(!sensor.has_state());
  CHECK(sensor.test_publish_count() == 0);
  CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
  CHECK(hub.status_has_warning());
  CHECK(bus.empty());
}

static void test_invalid_runtime_channel_has_no_side_effect() {
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubADC invalid(&hub, protocol::CHANNEL_COUNT);
  ready_hub(hub, bus);

  const size_t before = bus.transaction_count();
  invalid.update();
  hub.loop();
  CHECK(bus.transaction_count() == before);
  CHECK(!invalid.has_state());
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());
  CHECK(bus.empty());
}

int main() {
  set_unit_test_millis(1000);
  test_all_channels_are_serialized_and_little_endian();
  test_initial_failure_stays_unknown();
  test_invalid_runtime_channel_has_no_side_effect();

  if (failures != 0)
    std::cerr << failures << " ADC checks failed\n";
  return failures == 0 ? 0 : 1;
}
