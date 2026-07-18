# PBHUB v2 Clean-Break Notice

PBHUB component v2 intentionally replaces the prototype API instead of carrying
a compatibility layer. Existing v1 YAML must be rewritten against the
[v2 reference](../README.md#yaml-reference).

Three different version labels are involved:

- **PBHUB component v2** is this ESPHome component rewrite.
- **Application protocol version 2** is the byte reported by the PBHUB STM32 at
  register `0xFE` and is the only firmware protocol accepted by the component.
- **PBHUB v1.1 / U041-B** is the target M5Stack hardware revision.

## Support boundary

- ESPHome `2026.7.0` is the software-validation target. The component does not
  enforce that version. The channel/signal API has passed the complete repository
  validation and controller-build matrix against that release.
- ESP32 with ESP-IDF and Arduino are the supported framework targets.
- Real-hardware validation remains pending.
- An unreadable version is retried as a transport failure. A successfully read
  protocol v1, future or other value that is not `2` marks the component
  unsupported until the ESP controller restarts; there is no fallback.

## API replacements

| Removed prototype surface | v2 replacement |
|---|---|
| Generic `platform: gpio` with a nested PBHUB pin | Native `binary_sensor: platform: pbhub` or `switch: platform: pbhub`, with required `channel` and `signal` |
| Generic `platform: adc` with a PBHUB pin | Native `sensor: platform: pbhub` configured by `channel: 0..5`; signal A is fixed |
| `output: platform: pbhub_pwm` | `output: platform: pbhub` with required `channel`, `signal` and `mode: pwm` |
| PWM passed to ESPHome servo | `output: platform: pbhub` with required `channel`, `signal` and `mode: servo`, then a linked ESPHome servo |
| `light: platform: pbhub_rgb` | `light: platform: pbhub` with `pbhub_id`, `channel` and required `num_leds`; signal B is fixed |
| Old `pbhub:` child references | Canonical `pbhub_id:` references |
| Encoded `pin` values such as `31`, or ADC/RGB `slot` | Direct `channel: 0..5`; selectable features additionally require lowercase `signal: a` or `signal: b` |
| Multiple features silently sharing a signal | Configuration error identifying both channel/signal owners |

No aliases, warnings-only transition window or automatic normalization are
provided. Invalid old shapes fail configuration.

## Behavioral changes that require attention

1. Digital inputs are native polling entities, not generic GPIOs. They have no
   internal pull configuration or interrupt/edge capture.
2. Digital outputs are native switches and default to `ALWAYS_OFF`. Their state
   is a successfully transported command, not physical feedback.
3. ADC is fixed to signal A, configured by `channel`, exposes no `signal` choice
   and publishes raw `0..4095` values.
4. Every output requires an explicit `mode`. PWM remains fixed near a calculated
   nominal 392.16 Hz and cannot be used for RTTTL or servo control.
5. Servo mode writes direct `500..2500 us` pulses in the firmware's nominal
   20 ms frame; zero detaches. Neutral transforms and zero transition are
   required.
6. RGB is one uniform RGB light per channel, fixed to signal B. It exposes no
   `signal` choice. `num_leds: 1..74` is required; addressable and RGBW behavior
   are not provided.
7. One physical channel/signal pair may have only one owner. ADC claims signal A
   of its channel and RGB claims signal B, so those two features can coexist on
   the same channel.
8. The parent verifies firmware protocol version `2` before feature I/O and
   re-verifies it after detected communication loss.

## Deliberate exclusions

v2 does not expose persistent address changes, MCU reset, IAP, firmware flashing
or other firmware mutation. Same-bus hubs must be pre-addressed outside this
component; equal default addresses require isolated I2C multiplexer channels.

Review the [channel and ownership model](../README.md#channel-signal-and-ownership-model)
and [state and recovery semantics](../README.md#state-failure-and-recovery-semantics)
before deploying the new configuration.
