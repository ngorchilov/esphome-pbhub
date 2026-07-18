#include <array>
#include <cstdint>
#include <iostream>

#include "components/pbhub/pbhub_protocol.h"

using namespace esphome::pbhub::protocol;

static int failures = 0;

#define CHECK(...)                                                                                                     \
  do {                                                                                                                 \
    if (!(__VA_ARGS__)) {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #__VA_ARGS__ << '\n';                           \
      failures++;                                                                                                      \
    }                                                                                                                  \
  } while (false)

template<size_t N> bool unchanged(const WriteCommand<N> &command, uint8_t reg, const std::array<uint8_t, N> &payload) {
  return command.reg == reg && command.payload == payload;
}

int main() {
  constexpr std::array<uint8_t, 5> PWM_DUTIES{{0, 1, 127, 254, 255}};
  struct ServoPulseCase {
    uint16_t pulse_us;
    std::array<uint8_t, 2> payload;
  };
  constexpr std::array<ServoPulseCase, 3> SERVO_PULSES{{
      {500, {{0xF4, 0x01}}},
      {1500, {{0xDC, 0x05}}},
      {2500, {{0xC4, 0x09}}},
  }};

  for (uint16_t encoded = 0; encoded <= 255; encoded++) {
    Endpoint endpoint{0xEE, 0xEE};
    const bool expected = encoded <= 51 && encoded / 10 <= 5 && encoded % 10 <= 1;
    CHECK(decode_endpoint(static_cast<uint8_t>(encoded), endpoint) == expected);
    if (expected) {
      CHECK(endpoint.channel == encoded / 10);
      CHECK(endpoint.index == encoded % 10);
    } else {
      CHECK(endpoint.channel == 0xEE);
      CHECK(endpoint.index == 0xEE);
    }
  }

  std::array<bool, ENDPOINT_COUNT> ordinals{};
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
    for (uint8_t index = 0; index < 2; index++) {
      const Endpoint endpoint{channel, index};
      uint8_t ordinal = 0xFF;
      CHECK(endpoint_ordinal(endpoint, ordinal));
      CHECK(ordinal < ENDPOINT_COUNT);
      CHECK(!ordinals[ordinal]);
      ordinals[ordinal] = true;

      ReadCommand read{0xEE, 0xEE};
      CHECK(make_digital_read(endpoint, read));
      CHECK(read.reg == CHANNEL_BASES[channel] + 0x04 + index);
      CHECK(read.response_length == 1);

      WriteCommand<1> digital{};
      CHECK(make_digital_write(endpoint, true, digital));
      CHECK(digital.reg == CHANNEL_BASES[channel] + index);
      CHECK(digital.payload == std::array<uint8_t, 1>{{1}});

      for (const uint8_t duty : PWM_DUTIES) {
        WriteCommand<1> pwm{};
        CHECK(make_pwm_write(endpoint, duty, pwm));
        const bool digital_extreme = duty == 0 || duty == 255;
        CHECK(pwm.reg == CHANNEL_BASES[channel] + (digital_extreme ? 0x00 : 0x02) + index);
        CHECK(pwm.payload == std::array<uint8_t, 1>{{duty == 255 ? uint8_t{1} : duty}});
      }

      for (const auto &pulse : SERVO_PULSES) {
        WriteCommand<2> servo{};
        CHECK(make_servo_pulse_write(endpoint, pulse.pulse_us, servo));
        CHECK(servo.reg == CHANNEL_BASES[channel] + 0x0E + index);
        CHECK(servo.payload == pulse.payload);
      }

      WriteCommand<1> servo_detach{};
      CHECK(make_servo_detach_write(endpoint, servo_detach));
      CHECK(servo_detach.reg == CHANNEL_BASES[channel] + index);
      CHECK(servo_detach.payload == std::array<uint8_t, 1>{{0}});
    }

    ReadCommand adc{0xEE, 0xEE};
    CHECK(make_adc_read(channel, adc));
    CHECK(adc.reg == CHANNEL_BASES[channel] + 0x06);
    CHECK(adc.response_length == 2);

    WriteCommand<2> led_count{};
    CHECK(make_led_count_write(channel, 74, led_count));
    CHECK(led_count.reg == CHANNEL_BASES[channel] + 0x08);
    CHECK(led_count.payload == std::array<uint8_t, 2>{{74, 0}});

    WriteCommand<1> brightness{};
    CHECK(make_led_full_brightness_write(channel, brightness));
    CHECK(brightness.reg == CHANNEL_BASES[channel] + 0x0B);
    CHECK(brightness.payload == std::array<uint8_t, 1>{{255}});

    WriteCommand<7> fill{};
    CHECK(make_led_fill_write(channel, 74, 0, 74, {1, 2, 3}, fill));
    CHECK(fill.reg == CHANNEL_BASES[channel] + 0x0A);
    CHECK(fill.payload == std::array<uint8_t, 7>{{0, 0, 74, 0, 1, 2, 3}});
  }
  for (bool seen : ordinals)
    CHECK(seen);

  uint8_t unsafe_register = 0xEE;
  CHECK(!endpoint_register({0, 0}, static_cast<EndpointOperation>(0xBD), unsafe_register));
  CHECK(unsafe_register == 0xEE);
  CHECK(!endpoint_register({0, 0}, static_cast<EndpointOperation>(0xBF), unsafe_register));
  CHECK(unsafe_register == 0xEE);
  CHECK(!channel_register(0, static_cast<ChannelOperation>(0xBD), unsafe_register));
  CHECK(unsafe_register == 0xEE);

  CHECK(firmware_version_read().reg == 0xFE);
  CHECK(firmware_version_read().response_length == 1);
  CHECK(is_supported_firmware(2));
  CHECK(!is_supported_firmware(1));
  CHECK(!is_supported_firmware(3));
  CHECK(NOMINAL_PWM_FREQUENCY_HZ > 392.15f && NOMINAL_PWM_FREQUENCY_HZ < 392.17f);
  CHECK(SERVO_FRAME_US == 20000);
  CHECK(SERVO_MIN_PULSE_US == 500);
  CHECK(SERVO_MAX_PULSE_US == 2500);
  CHECK(NOMINAL_SERVO_FREQUENCY_HZ == 50.0f);
  CHECK(SERVO_MIN_LEVEL == 0.025f);
  CHECK(SERVO_MAX_LEVEL == 0.125f);
  CHECK(pwm_drive_mode(0) == PwmDriveMode::DIGITAL_LOW);
  CHECK(pwm_drive_mode(1) == PwmDriveMode::PWM);
  CHECK(pwm_drive_mode(254) == PwmDriveMode::PWM);
  CHECK(pwm_drive_mode(255) == PwmDriveMode::DIGITAL_HIGH);

  const Endpoint endpoint{2, 1};
  WriteCommand<1> pwm{};
  CHECK(make_pwm_write(endpoint, 0, pwm));
  CHECK(pwm.reg == 0x61);
  CHECK(pwm.payload[0] == 0);
  CHECK(make_pwm_write(endpoint, 1, pwm));
  CHECK(pwm.reg == 0x63);
  CHECK(pwm.payload[0] == 1);
  CHECK(make_pwm_write(endpoint, 254, pwm));
  CHECK(pwm.reg == 0x63);
  CHECK(pwm.payload[0] == 254);
  CHECK(make_pwm_write(endpoint, 255, pwm));
  CHECK(pwm.reg == 0x61);
  CHECK(pwm.payload[0] == 1);

  WriteCommand<2> servo{};
  CHECK(make_servo_pulse_write(endpoint, 500, servo));
  CHECK(servo.payload == std::array<uint8_t, 2>{{0xF4, 0x01}});
  CHECK(make_servo_pulse_write(endpoint, 2500, servo));
  CHECK(servo.payload == std::array<uint8_t, 2>{{0xC4, 0x09}});

  const WriteCommand<2> servo_sentinel{0xEE, {{0xAA, 0xBB}}};
  for (const uint16_t invalid_pulse : std::array<uint16_t, 5>{{0, 1, 499, 2501, 65535}}) {
    servo = servo_sentinel;
    CHECK(!make_servo_pulse_write(endpoint, invalid_pulse, servo));
    CHECK(unchanged(servo, 0xEE, std::array<uint8_t, 2>{{0xAA, 0xBB}}));
  }

  WriteCommand<2> count_sentinel{0xEE, {{0xAA, 0xBB}}};
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
    WriteCommand<2> count{};
    CHECK(make_led_count_write(channel, 1, count));
    CHECK(count.reg == CHANNEL_BASES[channel] + 0x08);
    CHECK(count.payload == std::array<uint8_t, 2>{{1, 0}});
    CHECK(make_led_count_write(channel, 74, count));
    CHECK(count.payload == std::array<uint8_t, 2>{{74, 0}});

    WriteCommand<1> brightness{};
    CHECK(make_led_full_brightness_write(channel, brightness));
    CHECK(brightness.reg == CHANNEL_BASES[channel] + 0x0B);
    CHECK(brightness.payload == std::array<uint8_t, 1>{{255}});
  }
  CHECK(!make_led_count_write(0, 0, count_sentinel));
  CHECK(unchanged(count_sentinel, 0xEE, std::array<uint8_t, 2>{{0xAA, 0xBB}}));
  CHECK(!make_led_count_write(0, 75, count_sentinel));
  CHECK(unchanged(count_sentinel, 0xEE, std::array<uint8_t, 2>{{0xAA, 0xBB}}));
  CHECK(!make_led_count_write(6, 1, count_sentinel));
  CHECK(unchanged(count_sentinel, 0xEE, std::array<uint8_t, 2>{{0xAA, 0xBB}}));
  WriteCommand<1> brightness_sentinel{0xEE, {{0xAA}}};
  CHECK(!make_led_full_brightness_write(6, brightness_sentinel));
  CHECK(unchanged(brightness_sentinel, 0xEE, std::array<uint8_t, 1>{{0xAA}}));

  WriteCommand<7> fill_sentinel{0xEE, {{1, 2, 3, 4, 5, 6, 7}}};
  const auto fill_before = fill_sentinel;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
    WriteCommand<7> fill{};
    CHECK(make_led_fill_write(channel, 74, 0, 74, {0, 127, 255}, fill));
    CHECK(fill.reg == CHANNEL_BASES[channel] + 0x0A);
    CHECK(fill.payload == std::array<uint8_t, 7>{{0, 0, 74, 0, 0, 127, 255}});
  }
  CHECK(make_led_fill_write(0, 1, 0, 1, {9, 8, 7}, fill_sentinel));
  CHECK(fill_sentinel.payload == std::array<uint8_t, 7>{{0, 0, 1, 0, 9, 8, 7}});
  CHECK(make_led_fill_write(5, 74, 73, 1, {9, 8, 7}, fill_sentinel));
  CHECK(fill_sentinel.reg == 0xAA);
  fill_sentinel = fill_before;
  CHECK(!make_led_fill_write(0, 0, 0, 1, {0, 0, 0}, fill_sentinel));
  CHECK(fill_sentinel.payload == fill_before.payload && fill_sentinel.reg == fill_before.reg);
  CHECK(!make_led_fill_write(0, 75, 0, 1, {0, 0, 0}, fill_sentinel));
  CHECK(!make_led_fill_write(0, 74, 0, 0, {0, 0, 0}, fill_sentinel));
  CHECK(!make_led_fill_write(0, 74, 74, 1, {0, 0, 0}, fill_sentinel));
  CHECK(!make_led_fill_write(0, 74, 70, 10, {0, 0, 0}, fill_sentinel));
  CHECK(!make_led_fill_write(0, 74, 0, 65535, {0, 0, 0}, fill_sentinel));
  CHECK(!make_led_fill_write(0, 74, 65535, 1, {0, 0, 0}, fill_sentinel));
  CHECK(!make_led_fill_write(6, 1, 0, 1, {0, 0, 0}, fill_sentinel));
  CHECK(fill_sentinel.payload == fill_before.payload && fill_sentinel.reg == fill_before.reg);

  WriteCommand<1> timing_sentinel{0xEE, {{0xAA}}};
  CHECK(make_led_timing_write(0, timing_sentinel));
  CHECK(timing_sentinel.reg == 0xFA && timing_sentinel.payload[0] == 0);
  CHECK(make_led_timing_write(1, timing_sentinel));
  CHECK(timing_sentinel.reg == 0xFA && timing_sentinel.payload[0] == 1);
  timing_sentinel = {0xEE, {{0xAA}}};
  CHECK(!make_led_timing_write(2, timing_sentinel));
  CHECK(unchanged(timing_sentinel, 0xEE, std::array<uint8_t, 1>{{0xAA}}));

  Endpoint invalid_endpoint{6, 0};
  ReadCommand read_sentinel{0xEE, 0xDD};
  CHECK(!make_digital_read(invalid_endpoint, read_sentinel));
  CHECK(read_sentinel.reg == 0xEE && read_sentinel.response_length == 0xDD);
  WriteCommand<1> write_sentinel{0xEE, {{0xAA}}};
  CHECK(!make_digital_write(invalid_endpoint, false, write_sentinel));
  CHECK(unchanged(write_sentinel, 0xEE, std::array<uint8_t, 1>{{0xAA}}));
  CHECK(!make_pwm_write({0, 2}, 127, write_sentinel));
  CHECK(unchanged(write_sentinel, 0xEE, std::array<uint8_t, 1>{{0xAA}}));
  CHECK(!make_servo_detach_write(invalid_endpoint, write_sentinel));
  CHECK(unchanged(write_sentinel, 0xEE, std::array<uint8_t, 1>{{0xAA}}));
  servo = servo_sentinel;
  CHECK(!make_servo_pulse_write(invalid_endpoint, 1500, servo));
  CHECK(unchanged(servo, 0xEE, std::array<uint8_t, 2>{{0xAA, 0xBB}}));

  bool digital = true;
  CHECK(decode_digital({{0}}, digital) && !digital);
  CHECK(decode_digital({{1}}, digital) && digital);
  CHECK(!decode_digital({{2}}, digital) && digital);

  uint16_t adc_value = 0xEEEE;
  CHECK(decode_adc({{0x00, 0x00}}, adc_value) && adc_value == 0);
  CHECK(decode_adc({{0xFF, 0x0F}}, adc_value) && adc_value == 4095);
  adc_value = 0xEEEE;
  CHECK(!decode_adc({{0x00, 0x10}}, adc_value) && adc_value == 0xEEEE);
  CHECK(!decode_adc({{0xFF, 0xFF}}, adc_value) && adc_value == 0xEEEE);

  if (failures != 0)
    std::cerr << failures << " protocol checks failed\n";
  return failures == 0 ? 0 : 1;
}
