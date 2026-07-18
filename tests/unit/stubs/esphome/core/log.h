#pragma once

#include <cstddef>

namespace esphome {

struct LogString {};

template<typename... Ts> inline void unit_test_log(Ts &&...) {}

inline size_t unit_test_warning_count = 0;
template<typename... Ts> inline void unit_test_warning(Ts &&...) { unit_test_warning_count++; }

}  // namespace esphome

#define LOG_STR(value) (reinterpret_cast<const ::esphome::LogString *>(value))
#define ESP_LOGE(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGW(...) ::esphome::unit_test_warning(__VA_ARGS__)
#define ESP_LOGI(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGD(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGV(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGVV(...) ::esphome::unit_test_log(__VA_ARGS__)
