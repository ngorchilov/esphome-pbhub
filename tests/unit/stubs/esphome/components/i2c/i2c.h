#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome::i2c {

enum ErrorCode {
  NO_ERROR = 0,
  ERROR_OK = 0,
  ERROR_INVALID_ARGUMENT = 1,
  ERROR_NOT_ACKNOWLEDGED = 2,
  ERROR_TIMEOUT = 3,
  ERROR_NOT_INITIALIZED = 4,
  ERROR_TOO_LARGE = 5,
  ERROR_UNKNOWN = 6,
  ERROR_CRC = 7,
};

class I2CBus {
 public:
  virtual ~I2CBus() = default;
  virtual ErrorCode write_readv(uint8_t address, const uint8_t *write_buffer, size_t write_count, uint8_t *read_buffer,
                                size_t read_count) = 0;
};

class I2CDevice {
 public:
  void set_i2c_address(uint8_t address) { this->address_ = address; }
  uint8_t get_i2c_address() const { return this->address_; }
  void set_i2c_bus(I2CBus *bus) { this->bus_ = bus; }

  ErrorCode read_register(uint8_t reg, uint8_t *data, size_t length) {
    return this->bus_->write_readv(this->address_, &reg, 1, data, length);
  }

  ErrorCode write_register(uint8_t reg, const uint8_t *data, size_t length) const {
    std::vector<uint8_t> buffer(length + 1);
    buffer[0] = reg;
    for (size_t i = 0; i < length; i++)
      buffer[i + 1] = data[i];
    return this->bus_->write_readv(this->address_, buffer.data(), buffer.size(), nullptr, 0);
  }

 protected:
  uint8_t address_{0};
  I2CBus *bus_{nullptr};
};

}  // namespace esphome::i2c

#define LOG_I2C_DEVICE(device) ESP_LOGCONFIG("i2c", "  Address: 0x%02X", (device)->get_i2c_address())
