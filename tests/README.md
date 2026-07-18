# Validation fixtures

The fixtures exercise the PBHUB component against exactly ESPHome 2026.7.0.
They contain no network credentials and use the repository's local
`components/` directory through a relative path. The runner also builds and
executes host-side protocol, ownership, recovery, scheduled-read serialization,
native digital-entity, all-slot ADC, fixed-frequency PWM, direct-pulse servo and
uniform-RGB scheduling, recovery and transport/health tests.

Run all configuration and expected-failure checks with:

```sh
python3 tools/validate.py
```

Add compilation of the complete ESP-IDF behavior suite and the paired-framework
core/topology matrix with:

```sh
python3 tools/validate.py --compile
```

Set `ESPHOME` to an alternative executable from an ESPHome 2026.7.0
environment when it is not available as `esphome` on `PATH`.

The default command proves pure protocol/ownership/recovery behavior, coalesced
FIFO polling with at most one scheduled input/ADC read per parent loop, native
binary-sensor and switch semantics, ADC little-endian publication and failure
preservation, scripted I2C transport and health transitions, and PWM transforms,
digital extrema, caching and recovery replay. The suite also covers direct 20 ms
servo-pulse conversion, digital-low detach, firmware-valid default and reversed
calibration, neutral-transform defenses, success-only caching and
recovery replay. Schema checks reject a configurable PBHUB frequency, RTTTL,
pseudo-servo through fixed PWM, out-of-range servo calibration or transforms, nonzero
servo transitions, duplicate Servo consumers and runtime power/turn-on actions
targeting a PBHUB servo output. RGB tests cover exact host-scaled byte boundaries,
firmware brightness 255, safe fills, explicit black, non-finite rejection,
coalescing, a fair parent-wide 50 ms fill cadence including timer wraparound,
global timing and stop-on-failure recovery ordering. Schema fixtures prove the
zero-transition default, explicit uniform effects/transitions and that LED timing
is a parent-only option. The optional compile pass proves ESPHome code generation
and integration for the hub-only, multi-hub, digital, PWM, servo, ADC, RGB and
ownership surfaces. Neither command replaces real PBHUB hardware validation;
ESPHome servo state and `has_reached_target()` are not transport or
physical-position feedback.

## Framework and topology matrix

Phase 7 makes classic ESP32 with ESP-IDF the detailed behavior target and adds a
small explicit parity matrix for framework-sensitive firmware compilation.

- Classic ESP32 with ESP-IDF is the primary configuration, code-generation,
  detailed compile and real-hardware target. Detailed positive/negative schema,
  generated-contract and logical fixtures run once on ESP-IDF.
- Two shared scenarios compile under both ESP-IDF and Arduino: a core-only
  configuration and a full-surface configuration exercising every PBHUB entity
  domain and feature guard, including PWM and servo code generation.
- The shared full-surface scenario covers direct I2C, two physical I2C buses,
  multiple hubs sharing one bus at distinct addresses and two TCA9548A virtual
  channels carrying hubs at the same default address. The `0x62` hubs represent
  devices pre-addressed outside this component; the component does not mutate
  device addresses.
- The runner verifies the resolved framework through the generated compiler
  definitions after each of the eleven firmware builds: nine ESP-IDF behavior
  fixtures and two Arduino parity fixtures.
- Arduino is a compile-only compatibility target. Hardware validation remains
  ESP-IDF-only unless Arduino runtime support is separately approved and tested.
- ESP8266 and ESP32-S3 are outside the v2 validation and support boundary.
