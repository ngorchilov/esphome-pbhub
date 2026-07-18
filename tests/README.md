# Validation fixtures

The fixtures exercise the PBHUB component against exactly ESPHome 2026.7.0.
They contain no network credentials and use the repository's local
`components/` directory through a relative path. The runner also builds and
executes host-side protocol, ownership, recovery, scheduled-read serialization,
native digital-entity, all-slot ADC, fixed-frequency PWM, direct-pulse servo and
transport/health tests.

Run all configuration and expected-failure checks with:

```sh
python3 tools/validate.py
```

Add compilation of every positive ESPHome fixture with:

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
targeting a PBHUB servo output. The optional compile pass proves ESPHome code
generation and integration for the hub-only, multi-hub, digital, PWM, servo,
ADC, RGB and ownership surfaces. Neither command replaces real PBHUB hardware
validation; ESPHome servo state and `has_reached_target()` are not transport or
physical-position feedback.
