#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/gpio.h"
#include <cstdint>
#include <string>

//
// User-provided symbolic addresses/opcodes
//
#define IIC_ADDR1 0x61
#define IIC_ADDR2 0x62
#define IIC_ADDR3 0x63
#define IIC_ADDR4 0x64
#define IIC_ADDR5 0x65
#define IIC_ADDR6 0x66
#define IIC_ADDR7 0x67
#define IIC_ADDR8 0x68

#define HUB1_ADDR 0x40
#define HUB2_ADDR 0x50
#define HUB3_ADDR 0x60
#define HUB4_ADDR 0x70
#define HUB5_ADDR 0x80
#define HUB6_ADDR 0xA0

// Per-channel function offsets (OR with base)
#define WRITE_DIGITAL_0 0x01 // index 0 ("A")
#define WRITE_DIGITAL_1 0x02 // index 1 ("B")
#define READ_DIGITAL_0 0x04
#define READ_DIGITAL_1 0x05
#define READ_ANALOG_0 0x06 // returns 2 bytes (lo, hi)
#define READ_ANALOG_1 0x07
#define SERVO_ANGLE_0 0x0C // 1 byte 0..180
#define SERVO_ANGLE_1 0x0D
#define SERVO_PULSE_0 0x0E // 2 bytes, little-endian microseconds
#define SERVO_PULSE_1 0x0F

namespace esphome
{
  namespace pbhub
  {

    class PbHubComponent : public Component, public i2c::I2CDevice
    {
    public:
      // Component
      void setup() override;
      void dump_config() override;

      // Simple helpers (slot/pin math)
      inline uint8_t slot_from_pin(uint8_t pin) const { return pin / 10; } // 30 -> 3
      inline uint8_t idx_from_pin(uint8_t pin) const { return pin % 10; }  // 30 -> 0

      // Base register for slot (0..5)
      inline uint8_t base_for_slot(uint8_t slot) const
      {
        return static_cast<uint8_t>(HUB1_ADDR + (slot * 0x10));
      }

      // -------- GPIO (digital) --------
      // No bit preservation: hub exposes per-channel registers.
      void pin_mode(uint8_t pin, uint8_t mode);        // kept for API symmetry (no-op)
      void digital_write_pin(uint8_t pin, bool state); // write 1 byte to base|WRITE_DIGITAL_x
      bool digital_read_pin(uint8_t pin);              // read 1 byte from base|READ_DIGITAL_x

      // -------- ADC --------
      // Reads 2 bytes (little-endian) from base|READ_ANALOG_x
      // Returns 0..1023 (or hub-native scale) — caller can map as needed.
      uint16_t analog_read_pin(uint8_t pin);

      // -------- Servo --------
      // Angle 0..180 (1 byte) to base|SERVO_ANGLE_x
      void servo_write_angle(uint8_t pin, uint8_t angle);
      // Pulse us (2 bytes little-endian) to base|SERVO_PULSE_x
      void servo_write_pulse_us(uint8_t pin, uint16_t micros);

      // Convenience: compute function register for slot+idx
      inline uint8_t reg_write_digital(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? WRITE_DIGITAL_0 : WRITE_DIGITAL_1);
      }
      inline uint8_t reg_read_digital(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? READ_DIGITAL_0 : READ_DIGITAL_1);
      }
      inline uint8_t reg_read_analog(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? READ_ANALOG_0 : READ_ANALOG_1);
      }
      inline uint8_t reg_servo_angle(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? SERVO_ANGLE_0 : SERVO_ANGLE_1);
      }
      inline uint8_t reg_servo_pulse(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? SERVO_PULSE_0 : SERVO_PULSE_1);
      }
    };

    // GPIOPin wrapper so you can use `pin: pbhub: ... number: 30` in YAML
    class PbHubGPIOPin : public GPIOPin
    {
    public:
      PbHubGPIOPin() = default;
      PbHubGPIOPin(PbHubComponent *parent, uint8_t pin) : parent_(parent), pin_(pin) {}

      // Required virtuals
      void setup() override;
      void pin_mode(gpio::Flags flags) override;
      bool digital_read() override;
      void digital_write(bool value) override;
      std::string dump_summary() const override;
      gpio::Flags get_flags() const override { return flags_; }

      // Extra setters
      void set_parent(PbHubComponent *p) { parent_ = p; }
      void set_pin(uint8_t pin) { pin_ = pin; }
      void set_inverted(bool inv) { inverted_ = inv; }
      void set_flags(gpio::Flags f) { flags_ = f; }

    protected:
      PbHubComponent *parent_{nullptr};
      uint8_t pin_{0}; // pbhub pin: slot*10 + idx
      bool inverted_{false};
      gpio::Flags flags_{gpio::FLAG_NONE};
    };

  } // namespace pbhub
} // namespace esphome