#pragma once

#include <cstdint>

namespace esphome {

inline uint32_t unit_test_millis = 0;
inline uint32_t unit_test_micros = 0;

inline uint32_t millis() { return unit_test_millis; }
inline uint32_t micros() { return unit_test_micros; }

inline void set_unit_test_millis(uint32_t value) {
  unit_test_millis = value;
  unit_test_micros = value * 1000U;
}

inline void set_unit_test_micros(uint32_t value) {
  unit_test_micros = value;
  unit_test_millis = value / 1000U;
}

}  // namespace esphome
