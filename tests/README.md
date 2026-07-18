# Validation fixtures

The fixtures exercise the PBHUB component against exactly ESPHome 2026.7.0.
They contain no network credentials and use the repository's local
`components/` directory through a relative path.

Run all configuration and expected-failure checks with:

```sh
python3 tools/validate.py
```

Add compilation of all Phase 1 positive fixtures with:

```sh
python3 tools/validate.py --compile
```

Set `ESPHOME` to an alternative executable from an ESPHome 2026.7.0
environment when it is not available as `esphome` on `PATH`.

All fixtures prove schema/configuration validation. The optional compile pass
also proves code generation and compilation for the hub-only, PWM, ADC and RGB
surfaces. Neither command claims that the legacy C++ entity behavior already
meets the Phase 2-6 runtime contracts.
