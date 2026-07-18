#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::pbhub::protocol {

inline constexpr uint8_t CHANNEL_COUNT = 6;
inline constexpr uint8_t ENDPOINT_COUNT = 12;
inline constexpr uint16_t MAX_LED_COUNT = 74;
inline constexpr uint8_t EXPECTED_FIRMWARE_VERSION = 2;
inline constexpr uint8_t REG_LED_TIMING = 0xFA;
inline constexpr uint8_t REG_FIRMWARE_VERSION = 0xFE;

inline constexpr std::array<uint8_t, CHANNEL_COUNT> CHANNEL_BASES{{0x40, 0x50, 0x60, 0x70, 0x80, 0xA0}};

struct Endpoint {
  uint8_t channel;
  uint8_t index;
};

struct Rgb {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

struct ReadCommand {
  uint8_t reg{};
  uint8_t response_length;
};

template<size_t N> struct WriteCommand {
  uint8_t reg{};
  std::array<uint8_t, N> payload;
};

enum class EndpointOperation : uint8_t {
  DIGITAL_WRITE = 0x00,
  PWM = 0x02,
  DIGITAL_READ = 0x04,
  SERVO_PULSE = 0x0E,
};

enum class ChannelOperation : uint8_t {
  ADC = 0x06,
  LED_COUNT = 0x08,
  LED_FILL = 0x0A,
  LED_BRIGHTNESS = 0x0B,
};

constexpr bool is_valid_channel(uint8_t channel) { return channel < CHANNEL_COUNT; }

constexpr bool is_valid(Endpoint endpoint) {
  return is_valid_channel(endpoint.channel) && endpoint.index < 2;
}

constexpr bool decode_endpoint(uint8_t encoded, Endpoint &out) {
  const Endpoint candidate{static_cast<uint8_t>(encoded / 10), static_cast<uint8_t>(encoded % 10)};
  if (!is_valid(candidate))
    return false;
  out = candidate;
  return true;
}

constexpr bool endpoint_ordinal(Endpoint endpoint, uint8_t &out) {
  if (!is_valid(endpoint))
    return false;
  out = static_cast<uint8_t>(endpoint.channel * 2 + endpoint.index);
  return true;
}

constexpr bool endpoint_register(Endpoint endpoint, EndpointOperation operation, uint8_t &out) {
  if (!is_valid(endpoint))
    return false;
  switch (operation) {
    case EndpointOperation::DIGITAL_WRITE:
    case EndpointOperation::PWM:
    case EndpointOperation::DIGITAL_READ:
    case EndpointOperation::SERVO_PULSE:
      break;
    default:
      return false;
  }
  out = static_cast<uint8_t>(CHANNEL_BASES[endpoint.channel] + static_cast<uint8_t>(operation) + endpoint.index);
  return true;
}

constexpr bool channel_register(uint8_t channel, ChannelOperation operation, uint8_t &out) {
  if (!is_valid_channel(channel))
    return false;
  switch (operation) {
    case ChannelOperation::ADC:
    case ChannelOperation::LED_COUNT:
    case ChannelOperation::LED_FILL:
    case ChannelOperation::LED_BRIGHTNESS:
      break;
    default:
      return false;
  }
  out = static_cast<uint8_t>(CHANNEL_BASES[channel] + static_cast<uint8_t>(operation));
  return true;
}

constexpr ReadCommand firmware_version_read() { return {REG_FIRMWARE_VERSION, 1}; }

constexpr bool make_digital_read(Endpoint endpoint, ReadCommand &out) {
  uint8_t reg{};
  if (!endpoint_register(endpoint, EndpointOperation::DIGITAL_READ, reg))
    return false;
  out = {reg, 1};
  return true;
}

constexpr bool make_adc_read(uint8_t channel, ReadCommand &out) {
  uint8_t reg{};
  if (!channel_register(channel, ChannelOperation::ADC, reg))
    return false;
  out = {reg, 2};
  return true;
}

constexpr bool make_digital_write(Endpoint endpoint, bool state, WriteCommand<1> &out) {
  uint8_t reg{};
  if (!endpoint_register(endpoint, EndpointOperation::DIGITAL_WRITE, reg))
    return false;
  out = {reg, {{static_cast<uint8_t>(state ? 1 : 0)}}};
  return true;
}

constexpr bool make_pwm_write(Endpoint endpoint, uint8_t duty, WriteCommand<1> &out) {
  if (duty == 0 || duty == 255)
    return make_digital_write(endpoint, duty == 255, out);

  uint8_t reg{};
  if (!endpoint_register(endpoint, EndpointOperation::PWM, reg))
    return false;
  out = {reg, {{duty}}};
  return true;
}

constexpr bool make_servo_pulse_write(Endpoint endpoint, uint16_t pulse_us, WriteCommand<2> &out) {
  if (pulse_us < 500 || pulse_us > 2500)
    return false;

  uint8_t reg{};
  if (!endpoint_register(endpoint, EndpointOperation::SERVO_PULSE, reg))
    return false;
  out = {reg, {{static_cast<uint8_t>(pulse_us & 0xFF), static_cast<uint8_t>(pulse_us >> 8)}}};
  return true;
}

constexpr bool make_led_count_write(uint8_t channel, uint16_t count, WriteCommand<2> &out) {
  if (count < 1 || count > MAX_LED_COUNT)
    return false;

  uint8_t reg{};
  if (!channel_register(channel, ChannelOperation::LED_COUNT, reg))
    return false;
  out = {reg, {{static_cast<uint8_t>(count & 0xFF), static_cast<uint8_t>(count >> 8)}}};
  return true;
}

constexpr bool make_led_full_brightness_write(uint8_t channel, WriteCommand<1> &out) {
  uint8_t reg{};
  if (!channel_register(channel, ChannelOperation::LED_BRIGHTNESS, reg))
    return false;
  out = {reg, {{0xFF}}};
  return true;
}

constexpr bool make_led_fill_write(uint8_t channel, uint16_t configured_count, uint16_t start, uint16_t count,
                                   Rgb color, WriteCommand<7> &out) {
  if (!is_valid_channel(channel) || configured_count < 1 || configured_count > MAX_LED_COUNT || count < 1 ||
      start >= configured_count || count > configured_count - start)
    return false;

  uint8_t reg{};
  if (!channel_register(channel, ChannelOperation::LED_FILL, reg))
    return false;
  out = {reg,
         {{static_cast<uint8_t>(start & 0xFF), static_cast<uint8_t>(start >> 8), static_cast<uint8_t>(count & 0xFF),
           static_cast<uint8_t>(count >> 8), color.red, color.green, color.blue}}};
  return true;
}

constexpr bool make_led_timing_write(uint8_t mode, WriteCommand<1> &out) {
  if (mode > 1)
    return false;
  out = {REG_LED_TIMING, {{mode}}};
  return true;
}

constexpr bool decode_digital(const std::array<uint8_t, 1> &raw, bool &value) {
  if (raw[0] > 1)
    return false;
  value = raw[0] != 0;
  return true;
}

constexpr bool decode_adc(const std::array<uint8_t, 2> &raw, uint16_t &value) {
  const uint16_t candidate = static_cast<uint16_t>(raw[0] | (static_cast<uint16_t>(raw[1]) << 8));
  if (candidate > 4095)
    return false;
  value = candidate;
  return true;
}

constexpr bool is_supported_firmware(uint8_t version) { return version == EXPECTED_FIRMWARE_VERSION; }

}  // namespace esphome::pbhub::protocol
