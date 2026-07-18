#pragma once

namespace esphome::output {

class FloatOutput {
 public:
  virtual ~FloatOutput() = default;
  void test_write_state(float state) { this->write_state(state); }

 protected:
  virtual void write_state(float state) = 0;
};

}  // namespace esphome::output
