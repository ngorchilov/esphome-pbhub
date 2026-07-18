#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#define USE_LIGHT
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
    payload.insert(payload.begin(), reg);
    this->expectations_.push_back({address, std::move(payload), 0, i2c::ERROR_OK, {}});
  }

  void expect_write_failure(uint8_t address, uint8_t reg, std::vector<uint8_t> payload, i2c::ErrorCode result) {
    payload.insert(payload.begin(), reg);
    this->expectations_.push_back({address, std::move(payload), 0, result, {}});
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

class CountingPollClient final : public PbHubPollClient {
 public:
  void perform_poll() override { this->polls_++; }
  int polls() const { return this->polls_; }

 protected:
  int polls_{0};
};

static light::LightState gray(uint8_t value) {
  const float level = static_cast<float>(value) / 255.0f;
  return {level, level, level};
}

static void test_scaling_coalescing_and_rate_limit() {
  set_unit_test_micros(1'000'000);
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubRGBLight rgb(&hub, 0);
  rgb.set_led_count(1);
  rgb.set_startup_off(true);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  hub.set_led_timing_mode(1);
  CHECK(hub.claim_endpoint(1, EndpointOwner::RGB, "rgb"));
  CHECK(hub.register_recovery_client(&rgb));

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, protocol::REG_LED_TIMING, {1});
  bus.expect_write(0x61, 0x48, {1, 0});
  bus.expect_write(0x61, 0x4B, {255});
  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 0, 0, 0});
  hub.setup();
  CHECK(hub.is_hub_ready());
  CHECK(bus.empty());

  auto black = gray(0);
  rgb.update_state(&black);
  rgb.write_state(&black);
  set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US);
  const size_t transactions_before_black_dedup = bus.transaction_count();
  hub.loop();
  CHECK(bus.transaction_count() == transactions_before_black_dedup);

  CountingPollClient poll_client;
  CHECK(hub.queue_poll(&poll_client));
  for (int repeat = 0; repeat < 3; repeat++) {
    rgb.update_state(&black);
    rgb.write_state(&black);
  }
  hub.loop();
  CHECK(poll_client.polls() == 1);

  auto value_127 = gray(127);
  auto value_128 = gray(128);
  auto value_254 = gray(254);
  rgb.update_state(&value_127);
  rgb.write_state(&value_127);
  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 127, 127, 127});
  hub.loop();

  rgb.update_state(&value_128);
  rgb.write_state(&value_128);
  rgb.update_state(&value_254);
  rgb.write_state(&value_254);
  set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US - 1);
  const size_t transactions_before_due = bus.transaction_count();
  hub.loop();
  CHECK(bus.transaction_count() == transactions_before_due);
  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 254, 254, 254});
  set_unit_test_micros(micros() + 1);
  hub.loop();

  for (uint8_t value : {uint8_t{128}, uint8_t{255}, uint8_t{0}}) {
    auto state = gray(value);
    rgb.update_state(&state);
    rgb.write_state(&state);
    bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, value, value, value});
    set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US);
    hub.loop();
  }

  const size_t transactions_before_nonfinite = bus.transaction_count();
  light::LightState invalid(std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f);
  rgb.update_state(&invalid);
  rgb.write_state(&invalid);
  set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US);
  hub.loop();
  CHECK(bus.transaction_count() == transactions_before_nonfinite);
  CHECK(hub.get_total_failures() == 0);
  CHECK(!hub.status_has_warning());

  const size_t transactions_before_invalid_fill = bus.transaction_count();
  CHECK(!hub.fill_leds(0, 1, 1, 1, 1, 2, 3));
  CHECK(bus.transaction_count() == transactions_before_invalid_fill);
  CHECK(hub.get_total_failures() == 0);

  auto red = light::LightState(1.0f, 0.0f, 0.0f);
  rgb.update_state(&red);
  rgb.write_state(&red);
  set_unit_test_micros(std::numeric_limits<uint32_t>::max() - 25'000U);
  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 255, 0, 0});
  hub.loop();

  auto blue = light::LightState(0.0f, 0.0f, 1.0f);
  rgb.update_state(&blue);
  rgb.write_state(&blue);
  set_unit_test_micros(10'000U);
  const size_t transactions_before_wrap_due = bus.transaction_count();
  hub.loop();
  CHECK(bus.transaction_count() == transactions_before_wrap_due);
  set_unit_test_micros(25'000U);
  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 0, 0, 255});
  hub.loop();

  CHECK(bus.empty());
}

static void test_parent_wide_fairness() {
  set_unit_test_micros(2'000'000);
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubRGBLight first(&hub, 0);
  PbHubRGBLight second(&hub, 1);
  first.set_led_count(1);
  second.set_led_count(2);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  CHECK(hub.claim_endpoint(1, EndpointOwner::RGB, "first"));
  CHECK(hub.claim_endpoint(11, EndpointOwner::RGB, "second"));
  CHECK(hub.register_recovery_client(&first));
  CHECK(hub.register_recovery_client(&second));

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, 0x48, {1, 0});
  bus.expect_write(0x61, 0x4B, {255});
  bus.expect_write(0x61, 0x58, {2, 0});
  bus.expect_write(0x61, 0x5B, {255});
  hub.setup();

  light::LightState red(1.0f, 0.0f, 0.0f);
  light::LightState green(0.0f, 1.0f, 0.0f);
  light::LightState blue(0.0f, 0.0f, 1.0f);
  first.update_state(&red);
  first.write_state(&red);
  second.update_state(&green);
  second.write_state(&green);

  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 255, 0, 0});
  hub.loop();
  first.update_state(&blue);
  first.write_state(&blue);

  set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US);
  bus.expect_write(0x61, 0x5A, {0, 0, 2, 0, 0, 255, 0});
  hub.loop();
  set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US);
  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 0, 0, 255});
  hub.loop();

  CountingPollClient poll_client;
  light::LightState yellow(1.0f, 1.0f, 0.0f);
  first.update_state(&yellow);
  first.write_state(&yellow);
  CHECK(hub.queue_poll(&poll_client));
  set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US);
  hub.loop();
  CHECK(poll_client.polls() == 1);

  CHECK(hub.queue_poll(&poll_client));
  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 255, 255, 0});
  hub.loop();

  light::LightState magenta(1.0f, 0.0f, 1.0f);
  first.update_state(&magenta);
  first.write_state(&magenta);
  set_unit_test_micros(micros() + PbHubComponent::RGB_MIN_REFRESH_INTERVAL_US);
  hub.loop();
  CHECK(poll_client.polls() == 2);
  bus.expect_write(0x61, 0x4A, {0, 0, 1, 0, 255, 0, 255});
  hub.loop();
  CHECK(bus.empty());
}

static void test_failed_fill_replays_latest_desired() {
  set_unit_test_micros(3'000'000);
  ScriptedI2CBus bus;
  PbHubComponent hub;
  PbHubRGBLight rgb(&hub, 2);
  rgb.set_led_count(3);

  hub.set_i2c_bus(&bus);
  hub.set_i2c_address(0x61);
  hub.set_led_timing_mode(0);
  CHECK(hub.claim_endpoint(21, EndpointOwner::RGB, "rgb"));
  CHECK(hub.register_recovery_client(&rgb));

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, protocol::REG_LED_TIMING, {0});
  bus.expect_write(0x61, 0x68, {3, 0});
  bus.expect_write(0x61, 0x6B, {255});
  hub.setup();

  light::LightState red(1.0f, 0.0f, 0.0f);
  rgb.update_state(&red);
  rgb.write_state(&red);
  bus.expect_write_failure(0x61, 0x6A, {0, 0, 3, 0, 255, 0, 0}, i2c::ERROR_NOT_ACKNOWLEDGED);
  hub.loop();
  CHECK(!hub.is_hub_ready());
  CHECK(hub.status_has_warning());
  CHECK(hub.test_has_timeout("firmware_probe"));

  light::LightState green(0.0f, 1.0f, 0.0f);
  light::LightState blue(0.0f, 0.0f, 1.0f);
  rgb.update_state(&green);
  rgb.write_state(&green);
  rgb.update_state(&blue);
  rgb.write_state(&blue);

  bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
  bus.expect_write(0x61, protocol::REG_LED_TIMING, {0});
  bus.expect_write(0x61, 0x68, {3, 0});
  bus.expect_write(0x61, 0x6B, {255});
  bus.expect_write(0x61, 0x6A, {0, 0, 3, 0, 0, 0, 255});
  CHECK(hub.test_run_timeout("firmware_probe"));
  CHECK(hub.is_hub_ready());
  CHECK(!hub.status_has_warning());
  CHECK(bus.empty());
}

static void test_recovery_stage_failures_stop_replay() {
  for (int failing_stage = 0; failing_stage < 4; failing_stage++) {
    set_unit_test_micros(4'000'000 + static_cast<uint32_t>(failing_stage) * 100'000U);
    ScriptedI2CBus bus;
    PbHubComponent hub;
    PbHubRGBLight rgb(&hub, 3);
    rgb.set_led_count(4);
    rgb.set_startup_off(true);

    hub.set_i2c_bus(&bus);
    hub.set_i2c_address(0x61);
    hub.set_led_timing_mode(1);
    CHECK(hub.claim_endpoint(31, EndpointOwner::RGB, "rgb"));
    CHECK(hub.register_recovery_client(&rgb));

    bus.expect_read(0x61, protocol::REG_FIRMWARE_VERSION, {protocol::EXPECTED_FIRMWARE_VERSION});
    if (failing_stage == 0) {
      bus.expect_write_failure(0x61, protocol::REG_LED_TIMING, {1}, i2c::ERROR_TIMEOUT);
    } else {
      bus.expect_write(0x61, protocol::REG_LED_TIMING, {1});
      if (failing_stage == 1) {
        bus.expect_write_failure(0x61, 0x78, {4, 0}, i2c::ERROR_TIMEOUT);
      } else {
        bus.expect_write(0x61, 0x78, {4, 0});
        if (failing_stage == 2) {
          bus.expect_write_failure(0x61, 0x7B, {255}, i2c::ERROR_TIMEOUT);
        } else {
          bus.expect_write(0x61, 0x7B, {255});
          bus.expect_write_failure(0x61, 0x7A, {0, 0, 4, 0, 0, 0, 0}, i2c::ERROR_TIMEOUT);
        }
      }
    }

    hub.setup();
    CHECK(!hub.is_hub_ready());
    CHECK(hub.get_hub_state() == HubState::UNVERIFIED);
    CHECK(hub.status_has_warning());
    CHECK(hub.get_total_failures() == 1);
    CHECK(hub.test_has_timeout("firmware_probe"));
    CHECK(bus.empty());
  }
}

int main() {
  test_scaling_coalescing_and_rate_limit();
  test_parent_wide_fairness();
  test_failed_fill_replays_latest_desired();
  test_recovery_stage_failures_stop_replay();

  if (failures != 0)
    std::cerr << failures << " PBHUB RGB checks failed\n";
  return failures == 0 ? 0 : 1;
}
