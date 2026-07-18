#pragma once

namespace esphome {

struct LogString {};

template<typename... Ts> inline void unit_test_log(Ts &&...) {}

}  // namespace esphome

#define LOG_STR(value) (reinterpret_cast<const ::esphome::LogString *>(value))
#define ESP_LOGE(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGW(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGI(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGD(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGV(...) ::esphome::unit_test_log(__VA_ARGS__)
#define ESP_LOGVV(...) ::esphome::unit_test_log(__VA_ARGS__)
