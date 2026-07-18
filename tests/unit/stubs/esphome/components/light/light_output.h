#pragma once

#include <initializer_list>

namespace esphome::light {

enum class ColorMode {
  RGB,
};

class LightTraits {
 public:
  void set_supported_color_modes(std::initializer_list<ColorMode>) {}
};

class LightState {
 public:
  LightState(float red, float green, float blue) : red_(red), green_(green), blue_(blue) {}

  void current_values_as_rgb(float *red, float *green, float *blue) {
    *red = this->red_;
    *green = this->green_;
    *blue = this->blue_;
  }

 protected:
  float red_;
  float green_;
  float blue_;
};

class LightOutput {
 public:
  virtual ~LightOutput() = default;
  virtual LightTraits get_traits() = 0;
  virtual void write_state(LightState *state) = 0;
};

}  // namespace esphome::light
