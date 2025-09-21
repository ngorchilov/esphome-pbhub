#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/gpio.h"
// #ifdef USE_OUTPUT
#include "esphome/components/output/float_output.h"
// #endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_LIGHT
#include "esphome/components/light/light_output.h"
#endif

#if !defined(USE_OUTPUT)
#warning "pbhub.cpp: USE_OUTPUT not defined in esphome/components/pbhub/pbhub.h"
#endif

namespace esphome
{
  namespace pbhub
  {

// ----------------------------------------------------
// Register Defines (match M5Unit-PbHub library)
// ----------------------------------------------------
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

#define WRITE_DIGITAL_0 0x00
#define WRITE_DIGITAL_1 0x01
#define WRITE_PWM_0 0x02
#define WRITE_PWM_1 0x03
#define READ_DIGITAL_0 0x04
#define READ_DIGITAL_1 0x05
#define READ_ANALOG_0 0x06
#define READ_ANALOG_1 0x07
#define LED_NUM 0x08
#define LED_COLOR 0x09
#define LED_FILL_COLOR 0x0A
#define LED_BRIGHTNESS 0x0B
#define SERVO_ANGLE_0 0x0C
#define SERVO_ANGLE_1 0x0D
#define SERVO_PULSE_0 0x0E
#define SERVO_PULSE_1 0x0F
#define LED_SHOW_MODE 0xFA
#define FW_VERSION 0xFE

    // ----------------------------------------------------
    // PbHubComponent (core driver)
    // ----------------------------------------------------
    class PbHubComponent : public Component, public i2c::I2CDevice
    {
    public:
      void setup() override;
      void dump_config() override;
      uint8_t get_fw_version();

      // -------- GPIO (digital) --------
      void pin_mode(uint8_t pin, uint8_t mode); // no-op
      void digital_write(uint8_t pin, bool state);
      bool digital_read(uint8_t pin);

      // -------- ADC --------
      uint16_t analog_read(uint8_t slot);

      // -------- PWM --------
      void set_pwm(uint8_t slot, uint8_t idx, uint8_t duty);

      // -------- Servo --------
      void set_servo_angle(uint8_t slot, uint8_t idx, uint8_t angle);
      void set_servo_pulse(uint8_t slot, uint8_t idx, uint16_t micros);

      // -------- RGB / LED --------
      void set_led_num(uint8_t slot, uint16_t count);
      void set_led_color(uint8_t slot, uint16_t index, uint8_t r, uint8_t g, uint8_t b);
      void fill_led_color(uint8_t slot, uint16_t start, uint16_t count, uint8_t r, uint8_t g, uint8_t b);
      void set_led_brightness(uint8_t slot, uint8_t value);
      void set_led_show_mode(uint8_t mode);
      uint8_t get_led_show_mode();

      // Helpers: slot/pin math
      inline uint8_t slot_from_pin(uint8_t pin) const { return pin / 10; } // 30 -> slot=3
      inline uint8_t idx_from_pin(uint8_t pin) const { return pin % 10; }  // 30 -> idx=0
      inline uint8_t base_for_slot(uint8_t slot) const
      {
        static const uint8_t BASES[6] = {HUB1_ADDR, HUB2_ADDR, HUB3_ADDR, HUB4_ADDR, HUB5_ADDR, HUB6_ADDR};
        return (slot < 6) ? BASES[slot] : HUB1_ADDR;
      }

      // Register address helpers
      inline uint8_t reg_write_digital(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? WRITE_DIGITAL_0 : WRITE_DIGITAL_1);
      }
      inline uint8_t reg_read_digital(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? READ_DIGITAL_0 : READ_DIGITAL_1);
      }
      inline uint8_t reg_read_analog(uint8_t slot) const
      {
        return base_for_slot(slot) | READ_ANALOG_0;
      }
      inline uint8_t reg_pwm(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? WRITE_PWM_0 : WRITE_PWM_1);
      }
      inline uint8_t reg_servo_angle(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? SERVO_ANGLE_0 : SERVO_ANGLE_1);
      }
      inline uint8_t reg_servo_pulse(uint8_t slot, uint8_t idx) const
      {
        return base_for_slot(slot) | (idx == 0 ? SERVO_PULSE_0 : SERVO_PULSE_1);
      }
      inline uint8_t reg_led_num(uint8_t slot) const
      {
        return base_for_slot(slot) | LED_NUM;
      }
      inline uint8_t reg_led_color(uint8_t slot) const
      {
        return base_for_slot(slot) | LED_COLOR;
      }
      inline uint8_t reg_led_fill(uint8_t slot) const
      {
        return base_for_slot(slot) | LED_FILL_COLOR;
      }
      inline uint8_t reg_led_brightness(uint8_t slot) const
      {
        return base_for_slot(slot) | LED_BRIGHTNESS;
      }
      // LED show mode and FW version are global, not slot-based.
      inline uint8_t reg_led_show_mode() const { return LED_SHOW_MODE; }
      inline uint8_t reg_fw_version() const { return FW_VERSION; }
    };

    // ----------------------------------------------------
    // GPIO Pin Wrapper
    // ----------------------------------------------------
    class PbHubGPIOPin : public GPIOPin
    {
    public:
      PbHubGPIOPin() = default;
      PbHubGPIOPin(PbHubComponent *parent, uint8_t pin) : parent_(parent), pin_(pin) {}

      void setup() override;
      void pin_mode(gpio::Flags flags) override;
      bool digital_read() override;
      void digital_write(bool value) override;
      std::string dump_summary() const override;
      gpio::Flags get_flags() const override { return flags_; }

      void set_parent(PbHubComponent *p) { parent_ = p; }
      void set_pin(uint8_t pin) { pin_ = pin; }
      void set_inverted(bool inv) { inverted_ = inv; }
      void set_flags(gpio::Flags f) { flags_ = f; }

    protected:
      PbHubComponent *parent_{nullptr};
      uint8_t pin_{0};
      bool inverted_{false};
      gpio::Flags flags_{gpio::FLAG_NONE};
    };

    // ----------------------------------------------------
    // PWM Output
    // ----------------------------------------------------
#ifdef USE_OUTPUT
    class PbHubPWMPin : public output::FloatOutput
#else
    class PbHubPWMPin
#endif
    {
    public:
      PbHubPWMPin(PbHubComponent *parent, uint8_t pin) : parent_(parent), pin_(pin) {}

#ifdef USE_OUTPUT
    protected:
      void write_state(float state) override;
#endif

      PbHubComponent *parent_;
      uint8_t pin_;
    };

    // ----------------------------------------------------
    // ADC Sensor
    // ----------------------------------------------------
#ifdef USE_SENSOR
    class PbHubADC : public sensor::Sensor, public PollingComponent
    {
    public:
      PbHubADC(PbHubComponent *parent, uint8_t slot, uint32_t update_interval = 1000);

      void update() override;

    protected:
      PbHubComponent *parent_;
      uint8_t slot_;
    };
#endif // USE_SENSOR

    // ----------------------------------------------------
    // Servo Output
    // ----------------------------------------------------
#ifdef USE_OUTPUT
    class PbHubServo : public output::FloatOutput
#else
    class PbHubServo
#endif
    {
    public:
      PbHubServo(PbHubComponent *parent, uint8_t pin) : parent_(parent), pin_(pin) {}

#ifdef USE_OUTPUT
    protected:
      void write_state(float state) override;
#endif

      PbHubComponent *parent_;
      uint8_t pin_;
    };

    // ----------------------------------------------------
    // RGB Light
    // ----------------------------------------------------
#ifdef USE_LIGHT
    class PbHubRGBLight : public light::LightOutput
    {
    public:
      PbHubRGBLight(PbHubComponent *parent, uint8_t slot) : parent_(parent), slot_(slot) {}

      light::LightTraits get_traits() override;
      void write_state(light::LightState *state) override;

      // NEW: allow YAML to specify LED count
      void set_led_count(uint16_t c)
      {
        led_count_ = c;
        initialized_ = false;
      }

    protected:
      PbHubComponent *parent_;
      uint8_t slot_;

      // NEW: state for LED strip configuration
      uint16_t led_count_{1};
      bool initialized_{false};
    };
#endif // USE_LIGHT
  } // namespace pbhub
} // namespace esphome