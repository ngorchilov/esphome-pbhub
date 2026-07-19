# Validation fixtures

The validation matrix targets ESPHome 2026.7.0; the component does not enforce
that version. These fixtures define the channel/signal API, and the complete
matrix has passed against that release. The fixtures contain no network
credentials and use the repository's local
`components/` directory through a relative path. The runner also builds and
executes host-side protocol, ownership, recovery, scheduled-read serialization,
native digital-entity, all-channel ADC, fixed-frequency PWM, direct-pulse servo
and uniform-RGB scheduling, recovery and transport/health tests. On this branch,
it also verifies the fixed-tone RTTTL gap policy and compiles the complete
play/stop/condition/callback integration surface.

Run all configuration and expected-failure checks with:

```sh
python3 tools/validate.py
```

Add compilation of the complete ESP-IDF behavior suite and the paired-framework
core/fixed-tone-RTTTL/topology matrix with:

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
recovery replay. Schema checks reject a configurable PBHUB frequency, RTTTL on
servo mode, pseudo-servo through fixed PWM, out-of-range servo calibration or transforms, nonzero
servo transitions, duplicate Servo consumers and runtime power/turn-on actions
targeting a PBHUB servo output. RGB tests cover exact host-scaled byte boundaries,
firmware brightness 255, safe fills, explicit black, non-finite rejection,
coalescing, a fair parent-wide 50 ms fill cadence including timer wraparound,
global timing and stop-on-failure recovery ordering. Schema fixtures prove the
zero-transition default, explicit uniform effects/transitions and that LED timing
is a parent-only option. The optional compile pass proves ESPHome code
generation and integration for the hub-only, multi-hub, digital, PWM, servo,
ADC, RGB and ownership surfaces. Neither command replaces real PBHUB hardware
validation; ESPHome servo state and `has_reached_target()` are not transport or
physical-position feedback.

## Framework and topology matrix

ESP32 with ESP-IDF is the detailed behavior target. A small explicit parity
matrix covers framework-sensitive firmware compilation under both supported
frameworks.

- ESP32 with ESP-IDF is the primary configuration, code-generation and detailed
  compile target, and the planned real-hardware target.
  Detailed positive/negative schema, generated-contract and controller firmware
  fixtures run once on ESP-IDF; strict host C++ tests remain framework-neutral.
- Three shared scenarios compile under both ESP-IDF and Arduino: a core-only
  configuration, the fixed-tone RTTTL integration and a full-surface
  configuration exercising every PBHUB entity domain and feature guard,
  including PWM and servo code generation.
- The shared full-surface scenario covers direct I2C, two physical I2C buses,
  multiple hubs sharing one bus at distinct addresses and two TCA9548A virtual
  channels carrying hubs at the same default address. The `0x62` hubs represent
  devices pre-addressed outside this component; the component does not mutate
  device addresses.
- The runner verifies the resolved framework through the generated compiler
  definitions after each of the thirteen ESPHome controller builds: ten
  ESP-IDF behavior fixtures and three Arduino parity fixtures.
- Arduino is a framework compile target. Planned hardware
  validation uses ESP-IDF unless Arduino runtime support is separately approved
  and tested.
- ESP8266 and ESP32-S3 are outside the v2 validation and support boundary.

## Public documentation provenance

The README's published v2 shapes are backed by the following passing fixtures:

| Public surface | Fixture authority |
|---|---|
| Minimal direct-bus hub | `common/esp32-idf.yaml`, `common/pbhub-core-only.yaml`, compiled by `positive/hub-only.yaml` |
| Conflict-free all-entity example | Composite of `common/esp32-idf.yaml` and the feature-hub portion of `common/pbhub-full-topology.yaml` |
| Digital input and output | `positive/digital-entities.yaml` |
| Raw ADC, channel with fixed signal A | `positive/adc-channels.yaml` |
| Fixed PWM | `positive/pwm-contract.yaml` |
| Fixed-tone RTTTL through PBHUB PWM | `positive/fixed-tone-rtttl.yaml` |
| Direct servo | `positive/servo-contract.yaml` |
| Uniform RGB | `positive/rgb-bounds.yaml` |
| Channel/signal selection, fixed ADC/RGB signals and ownership conflicts | `positive/all-channel-signals.yaml`, `positive/ownership.yaml` and matching negative fixtures |
| Two buses, same-bus addresses and TCA9548A channels | `common/esp32-idf-topology.yaml` and `common/pbhub-full-topology.yaml`, compiled by `positive/multi-hub.yaml` and `framework/arduino-topologies.yaml` |

The public installation block replaces the fixtures' repository-local external
component source with `github://ngorchilov/esphome-pbhub@v2-rtttl`. Publication also
renames test IDs, adds friendly entity names and omits optional transform fields
that exist only to widen fixture coverage. These transformations do not change
the PBHUB schemas or generated component paths exercised by the fixtures above.
