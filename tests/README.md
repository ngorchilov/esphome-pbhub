# Validation fixtures

The fixtures exercise the PBHUB component against exactly ESPHome 2026.7.0.
They contain no network credentials and use the repository's local
`components/` directory through a relative path. The runner also builds and
executes host-side protocol, ownership, recovery, scheduled-read serialization,
native digital-entity, all-slot ADC, fixed-frequency PWM and transport/health
tests.

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
digital extrema, caching and recovery replay. Schema checks also reject a
configurable PBHUB frequency, RTTTL use and pseudo-servo through fixed PWM. The
optional compile pass proves ESPHome code generation and integration for the
hub-only, multi-hub, digital, PWM, ADC, RGB and ownership surfaces. Neither
command replaces real PBHUB hardware validation.
