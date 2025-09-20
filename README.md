# ESPHome PBHUB Component

Custom ESPHome component for the M5Stack PBHUB / PortHub.

---

## Description and Features

This ESPHome component adds support for the [M5Stack PBHUB / PortHub](https://docs.m5stack.com/en/unit/pbhub), allowing you to easily expand the number of digital, analog, and PWM-capable I/O ports on your ESP32/ESP8266 device using I2C.

**Features:**

-   Digital GPIO (per-slot A/B pins, read/write)
-   Analog input (ADC)
-   PWM output (LED/servo control)
-   RGB LED support
-   Compatible with direct I2C or I2C multiplexers (e.g., PCA9548)

---

## Installation

You do **not** need to copy any source files manually.  
Instead, add this repository as an external component in your ESPHome YAML:

```yaml
external_components:
    - source: github://ngorchilov/esphome-pbhub@main
      refresh: 0s
      components: [pbhub]
```

---

## PBHUB Component Configuration

The PBHUB connects to your ESP device via I2C. You can use it either directly on your I2C bus, or through an I2C multiplexer (e.g., PCA9548) if you have multiple I2C devices with address conflicts or want to isolate buses.

### Direct Connection (SDA/SCL Pins)

```yaml
i2c:
    - id: bus_a
      sda: GPIO21
      scl: GPIO22
      frequency: 400kHz

pbhub:
    id: pb_hub
    i2c_id: bus_a
    address: 0x20 # Default PBHUB address
```

### Using an I2C Multiplexer (e.g., PCA9548)

```yaml
i2c:
    - id: bus_a
      sda: GPIO21
      scl: GPIO22
      frequency: 400kHz

pca9548:
    id: mux
    i2c_id: bus_a

pbhub:
    id: pb_hub
    i2c_id: mux.channel_3 # Select the multiplexer channel PBHUB is connected to
    address: 0x20
```

**Note:**  
You can have multiple PBHUBs, each with a different address or multiplexer channel.

---

## Understanding PBHUB Pin Numbers

PBHUB pin numbers are structured based on the slot number and the pin index within the slot. The formula is:

```
pin_number = slot × 10 + index
```

where `index` is `0` for pin A and `1` for pin B of each slot.

**Examples:**

-   Slot 3, pin A = 30
-   Slot 3, pin B = 31
-   Slot 5, pin A = 50
-   Slot 5, pin B = 51

This numbering scheme helps you easily identify and configure pins corresponding to specific slots and their A/B pins.

---

## Usage Examples

### 1. Digital GPIO (Read/Write)

You can use PBHUB pins as digital outputs or inputs, just like regular GPIOs:

```yaml
switch:
    - platform: gpio
      pin:
          pbhub: pb_hub
          number: 30
      name: 'Relay'

binary_sensor:
    - platform: gpio
      pin:
          pbhub: pb_hub
          number: 31
      name: 'Button'
```

### 2. Analog Input

Read analog values from PBHUB slots:

```yaml
sensor:
    - platform: adc
      pin:
          pbhub: pb_hub
          number: 32
      name: 'Analog Sensor'
      update_interval: 1s
```

### 3. PWM Output (Including Servo)

Use the custom PBHUB PWM platform to control LEDs, motors, or servos attached to PBHUB:

```yaml
output:
    - platform: pbhub_pwm
      pin:
          pbhub: pb_hub
          number: 33
      id: pwm_output

servo:
    - output: pwm_output
      id: servo_1
      min_pulse_width: 500us
      max_pulse_width: 2500us
```

### 4. RGB LED

Drive an RGB LED connected to PBHUB using the custom PBHUB RGB platform. You can specify the slot and optionally the number of LEDs:

```yaml
light:
    - platform: pbhub_rgb
      pbhub: pb_hub
      slot: 3
      num_leds: 1 # Optional, defaults to 1 if omitted
      name: 'RGB LED'
```

---

## Need Help?

For questions or issues, please open an issue on the [GitHub repository](https://github.com/ngorchilov/esphome-pbhub).

Happy automating!
