#include "pbhub.h"
#include "esphome/core/log.h"

// #if !defined(USE_OUTPUT)
// #warning "pbhub.cpp: USE_OUTPUT not defined in esphome/components/pbhub/pbhub.cpp"
// #endif

namespace esphome
{
  namespace pbhub
  {

    static const char *const TAG = "pbhub";

    // -----------------------------------------------------------------------------
    // PbHubComponent
    // -----------------------------------------------------------------------------
    void PbHubComponent::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up PBHUB at 0x%02X ...", this->address_);
    }

    void PbHubComponent::dump_config()
    {
      ESP_LOGCONFIG(TAG, "PBHUB:");
      ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
    }

    uint8_t PbHubComponent::get_fw_version()
    {
      uint8_t reg = reg_fw_version();
      uint8_t val = 0;
      auto err = this->write_read(&reg, 1, &val, 1);
      if (err != i2c::ERROR_OK)
      {
        ESP_LOGW(TAG, "FW_VERSION read failed (reg=0x%02X err=%d)", reg, err);
        return 0;
      }
      ESP_LOGI(TAG, "Firmware version: %u (reg=0x%02X)", val, reg);
      return val;

    } // -------- GPIO (digital) --------
    void PbHubComponent::pin_mode(uint8_t pin, uint8_t mode)
    {
      ESP_LOGD(TAG, "pin_mode(pin=%u, mode=%u) -> no-op", pin, mode);
    }

    void PbHubComponent::digital_write(uint8_t pin, bool state)
    {
      uint8_t slot = slot_from_pin(pin);
      uint8_t idx = idx_from_pin(pin);

      uint8_t reg = reg_write_digital(slot, idx);
      uint8_t val = state ? 0x01 : 0x00;

      auto err = this->write_register(reg, &val, 1);
      ESP_LOGD(TAG, "DWRITE pin %u (slot=%u idx=%u) -> %s (reg=0x%02X val=0x%02X err=%d)",
               pin, slot, idx, state ? "ON" : "OFF", reg, val, err);
    }

    bool PbHubComponent::digital_read(uint8_t pin)
    {
      uint8_t slot = slot_from_pin(pin);
      uint8_t idx = idx_from_pin(pin);

      uint8_t reg = reg_read_digital(slot, idx);
      uint8_t val = 0;
      auto err = this->write_read(&reg, 1, &val, 1);
      if (err != i2c::ERROR_OK)
      {
        ESP_LOGW(TAG, "DREAD failed: pin %u (slot=%u idx=%u) reg=0x%02X err=%d",
                 pin, slot, idx, reg, err);
        return false;
      }
      bool state = val & 0x01;
      ESP_LOGVV(TAG, "DREAD pin %u (slot=%u idx=%u) <- %s (reg=0x%02X val=0x%02X)",
                pin, slot, idx, state ? "ON" : "OFF", reg, val);
      return state;
    }

    // -------- ADC --------
    uint16_t PbHubComponent::analog_read(uint8_t slot)
    {
      uint8_t reg = reg_read_analog(slot);
      uint8_t buf[2] = {0, 0};
      auto err = this->write_read(&reg, 1, buf, 2);
      if (err != i2c::ERROR_OK)
      {
        ESP_LOGW(TAG, "AREAD failed: slot=%u reg=0x%02X err=%d", slot, reg, err);
        return 0;
      }
      uint16_t value = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
      ESP_LOGVV(TAG, "AREAD slot=%u <- %u (reg=0x%02X raw=%02X %02X)", slot, value, reg, buf[0], buf[1]);
      return value;
    }

    // -------- PWM --------
    void PbHubComponent::set_pwm(uint8_t slot, uint8_t idx, uint8_t duty)
    {
      uint8_t reg = reg_pwm(slot, idx);
      auto err = this->write_register(reg, &duty, 1);
      ESP_LOGVV(TAG, "PWM slot=%u idx=%u duty=%u (reg=0x%02X err=%d)", slot, idx, duty, reg, err);
    }

    // -------- Servo --------
    void PbHubComponent::set_servo_angle(uint8_t slot, uint8_t idx, uint8_t angle)
    {
      uint8_t reg = reg_servo_angle(slot, idx);
      auto err = this->write_register(reg, &angle, 1);
      ESP_LOGVV(TAG, "SERVO slot=%u idx=%u angle=%u (reg=0x%02X err=%d)", slot, idx, angle, reg, err);
    }

    void PbHubComponent::set_servo_pulse(uint8_t slot, uint8_t idx, uint16_t micros)
    {
      uint8_t reg = reg_servo_pulse(slot, idx);
      uint8_t data[2] = {uint8_t(micros & 0xFF), uint8_t(micros >> 8)};
      auto err = this->write_register(reg, data, 2);
      ESP_LOGVV(TAG, "SERVO slot=%u idx=%u pulse=%uus (reg=0x%02X data=%02X %02X err=%d)",
                slot, idx, micros, reg, data[0], data[1], err);
    }

    // -------- RGB Light --------
    void PbHubComponent::set_led_num(uint8_t slot, uint16_t count)
    {
      uint8_t reg = reg_led_num(slot);
      uint8_t data[2] = {uint8_t(count & 0xFF), uint8_t(count >> 8)};
      auto err = this->write_register(reg, data, 2);
      ESP_LOGD(TAG, "LED_NUM slot=%u count=%u (reg=0x%02X err=%d)", slot, count, reg, err);
    }

    void PbHubComponent::set_led_color(uint8_t slot, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
    {
      uint8_t reg = reg_led_color(slot);
      uint8_t data[5] = {
          uint8_t(index & 0xFF),
          uint8_t(index >> 8),
          r, g, b};
      auto err = this->write_register(reg, data, sizeof(data));
      ESP_LOGD(TAG, "LED_COLOR slot=%u index=%u color=(%u,%u,%u) (reg=0x%02X err=%d)",
               slot, index, r, g, b, reg, err);
    }

    void PbHubComponent::fill_led_color(uint8_t slot, uint16_t start, uint16_t count,
                                        uint8_t r, uint8_t g, uint8_t b)
    {
      uint8_t reg = reg_led_fill(slot);
      uint8_t data[7] = {
          uint8_t(start & 0xFF),
          uint8_t(start >> 8),
          uint8_t(count & 0xFF),
          uint8_t(count >> 8),
          r, g, b};
      auto err = this->write_register(reg, data, sizeof(data));
      ESP_LOGD(TAG, "LED_FILL slot=%u start=%u count=%u color=(%u,%u,%u) (reg=0x%02X err=%d)",
               slot, start, count, r, g, b, reg, err);
    }

    void PbHubComponent::set_led_brightness(uint8_t slot, uint8_t value)
    {
      uint8_t reg = reg_led_brightness(slot);
      auto err = this->write_register(reg, &value, 1);
      ESP_LOGD(TAG, "LED_BRIGHTNESS slot=%u value=%u (reg=0x%02X err=%d)", slot, value, reg, err);
    }

    void PbHubComponent::set_led_show_mode(uint8_t mode)
    {
      uint8_t reg = reg_led_show_mode();
      auto err = this->write_register(reg, &mode, 1);
      ESP_LOGD(TAG, "LED_SHOW_MODE mode=%u (reg=0x%02X err=%d)", mode, reg, err);
    }

    uint8_t PbHubComponent::get_led_show_mode()
    {
      uint8_t reg = reg_led_show_mode();
      uint8_t val = 0;
      auto err = this->write_read(&reg, 1, &val, 1);
      if (err != i2c::ERROR_OK)
      {
        ESP_LOGW(TAG, "LED_SHOW_MODE read failed (reg=0x%02X err=%d)", reg, err);
        return 0;
      }
      ESP_LOGD(TAG, "LED_SHOW_MODE read -> %u (reg=0x%02X)", val, reg);
      return val;
    }

    // -----------------------------------------------------------------------------
    // GPIOPin wrapper
    // -----------------------------------------------------------------------------
    void PbHubGPIOPin::setup()
    {
      ESP_LOGD(TAG, "GPIOPin setup pin=%u inverted=%d flags=0x%02X",
               pin_, inverted_, flags_);
    }

    void PbHubGPIOPin::pin_mode(gpio::Flags flags)
    {
      this->flags_ = flags;
      ESP_LOGD(TAG, "GPIOPin pin_mode pin=%u flags=0x%02X", pin_, flags_);
    }

    bool PbHubGPIOPin::digital_read()
    {
      if (!parent_)
        return false;
      bool raw = parent_->digital_read(pin_);
      return inverted_ ? !raw : raw;
    }

    void PbHubGPIOPin::digital_write(bool value)
    {
      if (!parent_)
        return;
      bool send = inverted_ ? !value : value;
      parent_->digital_write(pin_, send);
    }

    std::string PbHubGPIOPin::dump_summary() const
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "pbhub pin %u (inverted=%s)", pin_, inverted_ ? "yes" : "no");
      return std::string(buf);
    }

    // -----------------------------------------------------------------------------
    // PWM wrapper
    // -----------------------------------------------------------------------------
    // #ifdef USE_OUTPUT
    void PbHubPWMPin::write_state(float state)
    {
      if (!parent_)
        return;
      uint8_t duty = static_cast<uint8_t>(state * 255.0f);
      uint8_t slot = parent_->slot_from_pin(pin_);
      uint8_t idx = parent_->idx_from_pin(pin_);
      parent_->set_pwm(slot, idx, duty);
    }
    // #endif

    // -----------------------------------------------------------------------------
    // ADC wrapper
    // -----------------------------------------------------------------------------
#ifdef USE_SENSOR
    PbHubADC::PbHubADC(PbHubComponent *parent, uint8_t slot, uint32_t update_interval)
        : PollingComponent(update_interval), parent_(parent), slot_(slot) {}

    void PbHubADC::update()
    {
      if (!parent_)
        return;
      uint16_t val = parent_->analog_read(slot_);
      publish_state(val);
    }
#endif
    // -----------------------------------------------------------------------------
    // Servo wrapper
    // -----------------------------------------------------------------------------
    // #ifdef USE_OUTPUT
    void PbHubServo::write_state(float state)
    {
      if (!parent_)
        return;
      uint8_t angle = static_cast<uint8_t>(state * 180.0f);
      uint8_t slot = parent_->slot_from_pin(pin_);
      uint8_t idx = parent_->idx_from_pin(pin_);
      parent_->set_servo_angle(slot, idx, angle);
    }
    // #endif

    // -----------------------------------------------------------------------------
    // RGB Light wrapper
    // -----------------------------------------------------------------------------
#ifdef USE_LIGHT
    light::LightTraits PbHubRGBLight::get_traits()
    {
      auto traits = light::LightTraits();
      traits.set_supports_brightness(false);
      traits.set_supports_rgb(true);
      return traits;
    }

    void PbHubRGBLight::write_state(light::LightState *state)
    {
      if (!parent_)
        return;

      // Ensure LED count is set once
      if (!initialized_)
      {
        parent_->set_led_num(slot_, led_count_); // NEW/IMPORTANT: use led_count_
        initialized_ = true;
      }

      float r_f, g_f, b_f;
      state->current_values_as_rgb(&r_f, &g_f, &b_f);
      uint8_t r = static_cast<uint8_t>(r_f * 255.0f);
      uint8_t g = static_cast<uint8_t>(g_f * 255.0f);
      uint8_t b = static_cast<uint8_t>(b_f * 255.0f);

      parent_->fill_led_color(slot_, 0, led_count_, r, g, b);
    }
#endif // USE_LIGHT
  } // namespace pbhub
} // namespace esphome