#pragma once

#include <cstdint>

namespace esphome {

inline uint32_t unit_test_millis = 0;

inline uint32_t millis() { return unit_test_millis; }

inline void set_unit_test_millis(uint32_t value) { unit_test_millis = value; }

}  // namespace esphome
