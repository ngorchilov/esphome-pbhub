# ESPHome PBHUB v2

An ESPHome external component for the M5Stack Unit PBHUB v1.1 (SKU U041-B)
running the stock application protocol that reports firmware version `2`.

The v2 component provides native ESPHome entities for digital input, digital
output, raw ADC, fixed-frequency PWM, direct-pulse servo output and uniform RGB
strips. It validates the PBHUB's real endpoint model, prevents conflicting use
of one physical signal and restores desired outputs after a detected transport
failure.

> **Status:** the v2 software implementation and compile validation are complete
> on the `v2` branch. Real PBHUB hardware validation remains to be completed
> before release. Timing described as calculated or source-confirmed has not yet
> been measured on hardware.

## Scope and support

| Area | v2 support boundary |
|---|---|
| PBHUB device | M5Stack Unit PBHUB v1.1 / U041-B |
| PBHUB application protocol | Self-reported version `2` |
| ESPHome | Tested with `2026.7.0` |
| Frameworks | ESP-IDF and Arduino |
| Controller | ESP32 |

The firmware byte is a protocol compatibility guard. A response of `2` does not
authenticate an unmodified factory image. The component sends no feature command
until that value has been read successfully.

v2 is a deliberate clean break from the old component. It contains no legacy
YAML aliases, GPIOPin adapter or migration layer. See the
[v2 clean-break notice](docs/v2-clean-break.md) before replacing an existing
configuration.

## Installation and complete example

Until a v2 release is published, load the component from the `v2` branch.

The following conflict-free example covers every v2 entity type. Controller pins
and entity names are illustrative.

```yaml
esphome:
  name: pbhub-v2-example

esp32:
  board: esp32dev
  framework:
    type: esp-idf

logger:

external_components:
  - source: github://ngorchilov/esphome-pbhub@v2
    components: [pbhub]

i2c:
  id: main_i2c
  sda: GPIO21
  scl: GPIO22
  frequency: 100kHz

pbhub:
  id: hub
  i2c_id: main_i2c
  address: 0x61
  led_timing_mode: 0

binary_sensor:
  - platform: pbhub
    name: PBHUB Input
    pbhub_id: hub
    pin: 0
    inverted: false
    update_interval: 100ms

switch:
  - platform: pbhub
    name: PBHUB Output
    pbhub_id: hub
    pin: 1
    restore_mode: ALWAYS_OFF

sensor:
  - platform: pbhub
    name: PBHUB Raw ADC
    pbhub_id: hub
    slot: 1
    update_interval: 1s

output:
  - platform: pbhub
    id: hub_pwm
    pbhub_id: hub
    pin: 11
    mode: pwm

  - platform: pbhub
    id: hub_servo_output
    pbhub_id: hub
    pin: 20
    mode: servo

servo:
  - id: hub_servo
    output: hub_servo_output
    min_level: 2.5%
    idle_level: 7.5%
    max_level: 12.5%
    transition_length: 0s

light:
  - platform: pbhub
    name: PBHUB Uniform RGB Strip
    pbhub_id: hub
    slot: 2
    num_leds: 12
```

This allocation is conflict-free:

| Feature | Physical endpoint |
|---|---:|
| Digital input | `0` |
| Digital switch | `1` |
| ADC slot 1 | `10` (signal A) |
| PWM | `11` |
| Servo | `20` |
| RGB slot 2 | `21` (signal B) |

## Endpoint model and ownership

An endpoint is encoded as `slot * 10 + signal`, where the slot is `0..5` and
the signal is `0` for A or `1` for B.

| Slot | Signal A | Signal B |
|---:|---:|---:|
| 0 | `0` | `1` |
| 1 | `10` | `11` |
| 2 | `20` | `21` |
| 3 | `30` | `31` |
| 4 | `40` | `41` |
| 5 | `50` | `51` |

These twelve values are the only valid `pin` values. Numbers such as `2`, `19`,
`32` and `49` are invalid and fail configuration.

Each physical endpoint can have only one owner per hub:

- digital input, switch, PWM and servo own their configured `pin`;
- an ADC on `slot: N` owns signal A, endpoint `N * 10`;
- an RGB light on `slot: N` owns signal B, endpoint `N * 10 + 1`.

Two entities cannot claim the same endpoint on the same hub. The configuration
validator identifies both owners instead of allowing firmware mode changes to
make them fight at runtime.

## YAML reference

The tables list PBHUB-specific fields. Standard ESPHome entity fields remain
available unless a restriction is stated explicitly.

### Hub

```yaml
pbhub:
  id: hub
  i2c_id: main_i2c
  address: 0x61
  led_timing_mode: 0
```

| Option | Requirement and behavior |
|---|---|
| `id` | Parent ID referenced by each PBHUB entity. |
| `i2c_id` | ESPHome I2C bus carrying this hub. Specify it when selecting among buses. |
| `address` | Optional; defaults to the factory address `0x61`. It must match the address already programmed in the hub. |
| `led_timing_mode` | Optional integer `0` or `1`, global to all six RGB outputs. If omitted, the component does not write the global timing register. The firmware setting is nonpersistent and is restored during detected recovery when configured. |

Parent `setup_priority` is intentionally fixed by the component and is not a
supported YAML option.

Published LED-family associations are:

| Timing mode | Published device families |
|---:|---|
| `0` | WS2812, WS2815, WS2816, SK6812 |
| `1` | SK6822, APA106, PL9823 |

The stock firmware's exact waveform timing is build-dependent and has not yet
been verified on representative LEDs.

### Digital input

```yaml
binary_sensor:
  - platform: pbhub
    name: PBHUB Input
    pbhub_id: hub
    pin: 30
    inverted: false
    update_interval: 100ms
```

| Option | Default | Requirement and behavior |
|---|---:|---|
| `pbhub_id` | - | Required parent hub. |
| `pin` | - | Required exact endpoint. Reading it configures that signal as a floating input. |
| `inverted` | `false` | Applied only after a successful raw read. |
| `update_interval` | `100ms` | Polling interval. Short pulses between polls can be missed. |

The initial state remains unknown until a successful sample. A detected I2C or
protocol failure preserves the last published state.

PBHUB digital inputs are floating. The firmware exposes no internal pull
resistor, interrupt or edge-capture configuration. Add external biasing where
required, and remember that pulses between polls can be missed.

### Digital output switch

```yaml
switch:
  - platform: pbhub
    name: PBHUB Output
    pbhub_id: hub
    pin: 31
    inverted: false
    restore_mode: ALWAYS_OFF
```

| Option | Default | Requirement and behavior |
|---|---:|---|
| `pbhub_id` | - | Required parent hub. |
| `pin` | - | Required exact endpoint. |
| `inverted` | `false` | Standard ESPHome logical inversion. |
| `restore_mode` | `ALWAYS_OFF` | Logical-off command default; another ESPHome switch restore mode must be selected explicitly. |

Any restore mode that can turn the switch on may energize a connected load
during startup or recovery; choosing one is an explicit safety decision.

The switch publishes a new state only after the command was transported
successfully. It has no physical pin readback; the state is the last
successfully transported command, not proof that a connected load changed.

### Raw ADC sensor

```yaml
sensor:
  - platform: pbhub
    name: PBHUB Raw ADC
    pbhub_id: hub
    slot: 3
    update_interval: 1s
```

| Option | Default | Requirement and behavior |
|---|---:|---|
| `pbhub_id` | - | Required parent hub. |
| `slot` | - | Required integer `0..5`; ADC always uses signal A. |
| `update_interval` | `1s` | Polling interval. |

The sensor publishes a unitless raw integer from `0` through `4095`, with no
invented voltage conversion. A detected failure preserves the last value. Stock
firmware can return a stale but valid prior ADC response after an internal ADC
timeout, so a successful I2C transaction does not prove sample freshness.
Each firmware ADC request takes 22 samples and can clock-stretch I2C. Aggressive
polling can also worsen the stock firmware's PWM and servo timing.

### Fixed-frequency PWM output

```yaml
output:
  - platform: pbhub
    id: hub_pwm
    pbhub_id: hub
    pin: 11
    mode: pwm
```

`mode` is required. PWM accepts the standard ESPHome float-output transforms,
including `inverted`, `min_power`, `max_power` and `zero_means_zero`. The final
level `0.0..1.0` is rounded to a duty byte `0..255`.

The FloatOutput defaults are `inverted: false`, `min_power: 0%`,
`max_power: 100%` and `zero_means_zero: false`. If `min_power` is nonzero and
logical zero must remain electrically off, set `zero_means_zero: true`.

The firmware has no frequency control. Its source gives a calculated nominal
frequency of approximately **392.16 Hz**, but real timing and jitter remain
unmeasured. Encoded duty 0 and 255 are implemented by the component as digital
low and high; only values 1 through 254 use the firmware PWM register.

Do not use this output for RTTTL, an ESPHome servo or a load that requires a
different PWM frequency. A `frequency:` option is rejected. Runtime frequency
requests from a direct or future consumer are ignored with a warning. Standard
float-output level, on/off and power-transform actions remain available in PWM
mode.

PWM writes are immediate and do not use the RGB fill limiter. Rapid consumer
transitions can generate substantial I2C traffic and worsen the stock firmware's
PWM and servo timing.

### Direct-pulse servo output

```yaml
output:
  - platform: pbhub
    id: hub_servo_output
    pbhub_id: hub
    pin: 20
    mode: servo

servo:
  - id: hub_servo
    output: hub_servo_output
    min_level: 2.5%
    idle_level: 7.5%
    max_level: 12.5%
    transition_length: 0s
```

The PBHUB servo output interprets the ESPHome float level as a fraction of a
20,000 us frame and writes the firmware's direct pulse register. The accepted
nonzero range is `2.5%..12.5%`, corresponding to `500..2500 us`; exact zero
sends digital low and detaches the output.

Servo mode requires neutral output transforms:

- `inverted: false`;
- `min_power: 0%`;
- `max_power: 100%`;
- `zero_means_zero: true` (supplied by default in servo mode);
- `transition_length: 0s` on the linked ESPHome servo;
- at most one ESPHome servo consumer per PBHUB servo output.

Each linked servo field—`min_level`, `idle_level` and `max_level`—must
independently remain within `2.5%..12.5%`. Reversed ordering is supported.
ESPHome's standard `3%`, `7.5%` and `12%` defaults are within that range.

`servo.write`, `servo.detach`, a firmware-valid direct `output.set_level` and
`output.turn_off` are supported. `output.turn_on`, runtime min/max-power changes,
RTTTL and a YAML `frequency:` option are rejected for a servo-mode output. A
direct runtime frequency request is ignored with one warning and changes no
output state.

The levels are only the firmware-accepted pulse range. They are not guaranteed
mechanical limits for a connected actuator. Servo calibration must match the
actual mechanism. The source-derived nominal frame rate is 50 Hz; pulse timing
and jitter under load have not yet been measured on hardware.

### Uniform RGB light

```yaml
light:
  - platform: pbhub
    name: PBHUB Uniform RGB Strip
    pbhub_id: hub
    slot: 2
    num_leds: 12
```

| Option | Default | Requirement and behavior |
|---|---:|---|
| `pbhub_id` | - | Required parent hub. |
| `slot` | - | Required integer `0..5`; RGB always uses signal B. |
| `num_leds` | - | Required integer `1..74`. |
| `restore_mode` | `ALWAYS_OFF` | Standard ESPHome light restore mode with a logical-off command default. |
| `default_transition_length` | `0s` | Ordinary changes use one fill unless the user opts into transitions. |

`led_timing_mode` is a parent-hub option; it is not accepted on an individual
light.

All configured LEDs share one color. This is not an addressable light and does
not support RGBW or addressable effects. ESPHome applies on/off, brightness,
color brightness and gamma on the host; the component keeps the firmware's
defective brightness scaler at 255 and sends one bounds-checked fill.

Normal fills are coalesced and limited to one every 50 ms across the whole hub.
Polls and RGB work are scheduled fairly. This limiter reduces traffic but is not
a measured PWM or servo timing guarantee and may be revised after hardware
validation.
Standard non-addressable RGB effects and explicit transitions are accepted and
sampled through the same coalescing scheduler and hub-wide interval.

## Multiple hubs and I2C topology

The component supports multiple PBHUB instances across physical and virtual I2C
buses. Declare one `pbhub` entry for each physical hub. Every hub selects its bus
with `i2c_id`, and every PBHUB entity selects its parent with `pbhub_id`.

| Layout | Configuration | Address requirement |
|---|---|---|
| One hub on a direct bus | Point the hub at the physical I2C bus. | The configured address must match the hub. |
| Hubs on separate physical buses | Define multiple ESPHome I2C buses and assign each hub to the appropriate `i2c_id`. | The same address may be reused on different buses. |
| Multiple hubs on one physical bus | Assign every hub to the same `i2c_id`. | Every hub must already have a unique address. |
| Same-address hubs through a TCA9548A | Put each hub on a different multiplexer channel and use that channel's virtual bus ID as `i2c_id`. | The default `0x61` address may be reused on isolated channels. |

For example, two hubs with different addresses can share one physical bus:

```yaml
pbhub:
  - id: hub_1
    i2c_id: main_i2c
    address: 0x61

  - id: hub_2
    i2c_id: main_i2c
    address: 0x62
```

The component does not change a hub's persistent address. In this example,
`hub_2` must already have been programmed to respond at `0x62`; the YAML only
tells ESPHome which address to use.

Hubs that retain the same default address can instead use isolated TCA9548A
channels:

```yaml
tca9548a:
  id: pbhub_multiplexer
  i2c_id: main_i2c
  address: 0x70
  channels:
    - bus_id: mux_bus_0
      channel: 0
    - bus_id: mux_bus_1
      channel: 1

pbhub:
  - id: hub_1
    i2c_id: mux_bus_0
    address: 0x61

  - id: hub_2
    i2c_id: mux_bus_1
    address: 0x61
```

The hubs are independent parents: endpoint ownership, communication health and
output recovery are tracked separately for each `pbhub` ID.

## State, failure and recovery semantics

| Entity | What its ESPHome state means |
|---|---|
| Binary sensor | Last successfully transported and decoded input sample. |
| ADC sensor | Last successfully transported 12-bit response; firmware-level staleness can be undetectable. |
| Switch | Last digital-output command transported successfully, not physical feedback. |
| PWM output | No published entity state; the driver retains desired and last-successfully-applied encoded values. |
| Servo / `has_reached_target()` | Host command progression, not I2C acknowledgement or physical position. |
| RGB light | ESPHome desired light state; it can update before a queued I2C fill succeeds. |

On startup, the parent reads register `0xFE`. An unreadable version is treated as
a recoverable transport failure and retried every five seconds. Repeated failure
logs are throttled to thirty seconds. A successfully read value other than `2`
marks the component unsupported until the ESP controller restarts.

A detected transport or protocol failure immediately raises the parent warning,
invalidates firmware verification and all applied-state caches, then schedules a
new version probe. After version `2` is verified again, the component restores
configured global RGB timing, strip count and brightness configuration before
replaying the latest desired switch, PWM, servo and RGB states. Input and ADC
entities preserve their last published values while communication is
unavailable.

The hub exposes no reset counter. A reset that begins and completes entirely
between successful host transactions is therefore undetectable. Such a reset can
erase active hub modes without immediately changing ESPHome state. Only a
detected failure invokes full recovery. A changed, non-deduplicated output value
can also issue a new command, but reissuing an identical state may be skipped by
the still-valid host cache and cannot be relied upon for recovery.

An I2C ACK proves byte transport only. The stock firmware has no semantic
acknowledgement or physical-output feedback.

## Deliberately unsupported

- PBHUB firmware protocol versions other than self-reported version `2`.
- Legacy component YAML, generic GPIOPin use, `pbhub_pwm` or `pbhub_rgb`.
- Internal input pulls, interrupts, firmware-side debounce or edge capture.
- Variable-frequency PWM, RTTTL and pseudo-servo through PWM.
- Addressable RGB effects or RGBW output.
- Runtime I2C address mutation, MCU reset, IAP or firmware flashing.
- Multiple entities owning one physical endpoint.

## Validation and technical references

The current software validation was run with ESPHome 2026.7.0. The component has
no explicit ESPHome version constraint; other releases may work but have not
been tested. The complete component surface configures and compiles with both
ESP-IDF and Arduino on ESP32. Strict host C++ tests cover protocol construction,
ownership, scheduling, recovery and entity state behavior independently of the
ESP32 framework.

Public example provenance is recorded in the
[fixture guide](tests/README.md). The main sources of truth are:

- [PBHUB v1.1 internal firmware protocol datasheet](docs/pbhub-firmware-protocol.md)
- [ESPHome PBHUB v2 implementation plan](docs/v2-implementation-plan.md)
- [v2 clean-break notice](docs/v2-clean-break.md)
- [validation fixtures and commands](tests/README.md)

Software tests and source analysis do not replace the
[planned hardware validation](docs/v2-implementation-plan.md#required-real-hardware-matrix).

For questions or defects, open an issue in this repository.
