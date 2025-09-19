#include "pbhub.h"
#include "esphome/core/log.h"

namespace esphome
{
  namespace pbhub
  {

    static const char *const TAG = "pbhub";

    void PbHubComponent::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up PBHUB at 0x%02X (native I2CDevice)...", this->address_);
      // No special init sequence required from original driver for GPIO/ADC/Servo
    }

    void PbHubComponent::dump_config()
    {
      ESP_LOGCONFIG(TAG, "PBHUB:");
      ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
    }

    // ---------------- GPIO (digital) ----------------

    void PbHubComponent::pin_mode(uint8_t pin, uint8_t mode)
    {
      // Hub is register-driven per channel; no separate mode command required here.
      ESP_LOGD(TAG, "PINMODE pin %u -> mode=%u (no-op for PBHub GPIO)", pin, mode);
    }

    void PbHubComponent::digital_write_pin(uint8_t pin, bool state)
    {
      const uint8_t slot = this->slot_from_pin(pin);
      const uint8_t idx = this->idx_from_pin(pin);
      const uint8_t reg = this->reg_write_digital(slot, idx);

      const uint8_t val = state ? 0x01 : 0x00;
      auto err = this->write_register(reg, &val, 1);

      ESP_LOGD(TAG,
               "DWRITE pin %u (slot %u idx %u) -> %s (reg=0x%02X, val=0x%02X, err=%d)",
               pin, slot, idx, state ? "ON" : "OFF", reg, val, err);
    }

    bool PbHubComponent::digital_read_pin(uint8_t pin)
    {
      const uint8_t slot = this->slot_from_pin(pin);
      const uint8_t idx = this->idx_from_pin(pin);
      const uint8_t reg = this->reg_read_digital(slot, idx);

      uint8_t val = 0;
      auto err = this->read_register(reg, &val, 1);
      if (err != i2c::ERROR_OK)
      {
        ESP_LOGW(TAG, "DREAD failed: pin %u (slot %u idx %u) reg=0x%02X err=%d",
                 pin, slot, idx, reg, err);
        return false;
      }
      const bool state = (val != 0);
      ESP_LOGD(TAG, "DREAD pin %u (slot %u idx %u) <- %s (reg=0x%02X, raw=0x%02X)",
               pin, slot, idx, state ? "ON" : "OFF", reg, val);
      return state;
    }

    // ---------------- ADC ----------------

    uint16_t PbHubComponent::analog_read_pin(uint8_t pin)
    {
      const uint8_t slot = this->slot_from_pin(pin);
      const uint8_t idx = this->idx_from_pin(pin);
      const uint8_t reg = this->reg_read_analog(slot, idx);

      uint8_t buf[2] = {0, 0};
      auto err = this->read_register(reg, buf, 2);
      if (err != i2c::ERROR_OK)
      {
        ESP_LOGW(TAG, "AREAD failed: pin %u (slot %u idx %u) reg=0x%02X err=%d",
                 pin, slot, idx, reg, err);
        return 0;
      }
      // Assume little-endian (lo, hi)
      const uint16_t value = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
      ESP_LOGD(TAG, "AREAD pin %u (slot %u idx %u) <- %u (reg=0x%02X, bytes=%02X %02X)",
               pin, slot, idx, value, reg, buf[0], buf[1]);
      return value;
    }

    // ---------------- Servo ----------------

    void PbHubComponent::servo_write_angle(uint8_t pin, uint8_t angle)
    {
      const uint8_t slot = this->slot_from_pin(pin);
      const uint8_t idx = this->idx_from_pin(pin);
      const uint8_t reg = this->reg_servo_angle(slot, idx);

      auto a = angle;
      auto err = this->write_register(reg, &a, 1);
      ESP_LOGD(TAG, "SERVO angle pin %u (slot %u idx %u) -> %u (reg=0x%02X, err=%d)",
               pin, slot, idx, angle, reg, err);
    }

    void PbHubComponent::servo_write_pulse_us(uint8_t pin, uint16_t micros)
    {
      const uint8_t slot = this->slot_from_pin(pin);
      const uint8_t idx = this->idx_from_pin(pin);
      const uint8_t reg = this->reg_servo_pulse(slot, idx);

      uint8_t buf[2] = {static_cast<uint8_t>(micros & 0xFF),
                        static_cast<uint8_t>((micros >> 8) & 0xFF)};
      auto err = this->write_register(reg, buf, 2);
      ESP_LOGD(TAG, "SERVO pulse pin %u (slot %u idx %u) -> %u us (reg=0x%02X, data=%02X %02X, err=%d)",
               pin, slot, idx, micros, reg, buf[0], buf[1], err);
    }

    // ---------------- GPIOPin wrapper ----------------

    void PbHubGPIOPin::setup()
    {
      if (!parent_)
        return;
      ESP_LOGD(TAG, "GPIOPin setup pin=%u inverted=%d flags=0x%02X", pin_, inverted_, flags_);
      // No explicit mode config needed on PBHub for simple digital use.
    }

    void PbHubGPIOPin::pin_mode(gpio::Flags flags)
    {
      flags_ = flags;
      ESP_LOGD(TAG, "GPIOPin pin_mode pin=%u flags=0x%02X", pin_, flags_);
    }

    bool PbHubGPIOPin::digital_read()
    {
      if (!parent_)
        return false;
      bool raw = parent_->digital_read_pin(pin_);
      return inverted_ ? !raw : raw;
    }

    void PbHubGPIOPin::digital_write(bool value)
    {
      if (!parent_)
        return;
      bool send = inverted_ ? !value : value;
      parent_->digital_write_pin(pin_, send);
    }

    std::string PbHubGPIOPin::dump_summary() const
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "pbhub pin %u (inverted=%s)", pin_, inverted_ ? "yes" : "no");
      return std::string(buf);
    }

  } // namespace pbhub
} // namespace esphome