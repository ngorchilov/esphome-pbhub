# ESPHome PBHUB v2 Implementation Plan

This is the implementation plan for the `v2` branch. The overhaul is intentionally
split into reviewable phases. The protocol and safety decisions come from
[the firmware datasheet](pbhub-firmware-protocol.md), which remains the source of
truth when implementation details are debated later.

This phase changes documentation only. It does not yet change component behavior.

## Goals

1. Implement the real STM32 protocol instead of treating the PBHUB like an
   ordinary local GPIO expander.
2. Represent transport failure separately from valid `false` and zero readings.
3. Prevent invalid endpoint mapping and conflicting ownership of physical pins.
4. Expose digital input/output, ADC, fixed-frequency PWM, servo and uniform RGB
   light through native ESPHome entities.
5. Keep the component efficient enough that I2C traffic does not unnecessarily
   worsen the firmware's PWM and servo jitter.
6. Target only stock PBHUB application firmware version 2.
7. Target the public APIs of ESPHome 2026.7.0 and require every selected
   framework/target fixture to compile under that version.
8. Publish only portable product and implementation documentation. Exclude
   machine-local paths, deployment identifiers and workflow history.

## Non-goals for ESPHome v2

- Changing or bundling the PBHUB's STM32 firmware.
- Supporting PBHUB application firmware other than version 2.
- Preserving pre-v2 component schemas, YAML syntax or C++ adapters.
- Exposing firmware flashing, reset-to-IAP or runtime I2C address mutation.
- Claiming variable-frequency PWM or working RTTTL playback on stock application
  firmware version 2.
- Pretending the RGB protocol is an efficient addressable-light transport.
- Adding tracked dotfiles or repository-hosted CI while the project's deliberate
  `.*` exclusion remains in place.
- Supporting ESPHome releases other than 2026.7.0.

## Design decisions

| Area | v2 decision | Reason |
|---|---|---|
| Endpoint model | Exact channel/index type; valid external numbers are only `0,1,10,11,...,50,51` | The register banks are non-contiguous and invalid values currently fall back to channel 0 |
| Digital input | Native polling `binary_sensor` | A boolean GPIOPin API cannot report I2C failure and generic GPIO polling is excessively frequent |
| Digital output | Native `switch` only | The clean v2 API does not retain the old GPIOPin abstraction |
| ADC | Native raw sensor configured by `slot: 0..5` | ADC exists only on signal A, not on an arbitrary endpoint |
| PWM | `output` mode with documented fixed frequency near 392 Hz | Application firmware version 2 has no frequency control |
| Servo | Separate `output` mode using the direct pulse register | Application firmware version 2 has a genuine 50 Hz servo generator; ordinary PWM is not equivalent |
| RGB | One uniform RGB light per configured channel/strip | Every indexed command immediately transmits a strip prefix, making addressable effects inefficient |
| RGB brightness | Scale on the ESP host; keep STM32 brightness at 255 | Firmware brightness is nonlinear and applies one color write late |
| Endpoint conflicts | Reject duplicate ownership | Firmware commands silently replace the current mode of a physical signal |
| Dangerous globals | Excluded | Address mutation wears flash, and reset/IAP is outside the ESPHome component's scope |
| Firmware target | Application firmware version exactly 2 | No behavior is assumed for v1, future or unknown firmware |
| Transport API | ESPHome 2026.7.0 `read_register`/`write_register`, returning `i2c::ErrorCode` | These current APIs directly match the PBHUB register transaction model |
| ESPHome target | Exactly 2026.7.0 | No cross-version shims or historical API branches are required |

## Current implementation gap analysis

The present component is a useful prototype, but these issues must be resolved
before it can be called solid.

### 1. Endpoint validation accepts impossible pins

Python schemas generally accept every integer from 0 through 51. Values such as
2, 9, 22 and 49 do not represent a physical PBHUB signal. C++ register helpers
also map an invalid channel back to channel 0. A typo can therefore control the
wrong output instead of failing configuration.

v2 action: one shared validator must accept only the twelve exact endpoint values,
and no C++ helper may have a channel-0 fallback.

### 2. ADC configuration and C++ interpretation disagree

The current sensor schema accepts a pin-shaped number, while the C++ sensor passes
that number as a channel. A value such as 32 is not channel 3; it becomes an
invalid channel and currently falls back to channel 0.

v2 action: ADC uses `slot: 0..5`. Signal A is implicit because `B+0x6` is the only
implemented ADC register.

### 3. RGB configuration is not implemented as advertised

The light schema accepts `slot` values through 51 even though the C++ code expects
0 through 5. `led_count` changes an internal flag but the light never programs the
count or uses a range fill. Each update writes only LED 0. It writes color before
brightness, while firmware brightness affects only subsequent color writes.

v2 action: validate slot and count, initialize count once, force firmware
brightness 255, host-scale color and use one bounded fill for all configured LEDs.

### 4. Communication failure is converted into valid state

Digital read returns `false` on I2C error and ADC returns zero. An inverted input
can turn a failed read into `true`, and a real ADC zero cannot be distinguished
from disconnection.

v2 action: transport methods return success separately from an output value.
Sensors preserve their last value on failure and the parent component reports a
warning state.

This fixes host-detected transport failures only. A firmware ADC timeout can
leave a stale but valid prior response in the TX buffer while the I2C transaction
succeeds. Application firmware version 2 provides no sequence number or status
bit with which the host could prove sample freshness; v2 must document that
irreducible limitation.

### 5. Write failures are logged but not reflected in entities

Most write methods ignore the result after logging it. Switch/light/output state
can appear updated even when the hub did not accept the transaction.

v2 action: centralize result handling. Cache a new output only after a successful
write, publish switch state only after success and retain a retryable desired state
where the ESPHome entity API permits it.

### 6. Generic GPIO input is the wrong abstraction

ESPHome's generic GPIO binary sensor can call a non-interrupt pin's
`digital_read()` on every main-loop pass. Every call becomes an I2C transaction
and also reasserts input mode in the STM32 firmware. Several inputs can create
continuous bus traffic and worsen PWM/servo timing.

v2 action: native PBHUB binary sensors poll at an explicit interval and publish
only successful samples. Remove the custom GPIOPin adapter entirely; digital
outputs use the native PBHUB switch.

### 7. PWM, servo and RTTTL are conflated

The current output platform always writes the PBHUB PWM duty register. The
unregistered C++ servo wrapper is not reachable from YAML. Feeding that PWM
output to ESPHome `servo` does not select the hub's 50 Hz servo mode. Feeding it
to RTTTL cannot change the firmware's fixed approximately 392 Hz frequency, so
notes do not follow the melody.

v2 action: make `mode: pwm` and `mode: servo` explicit. Dynamic frequency requests
must not fail silently. RTTTL remains unsupported on application firmware version
2 and the README must say so.

### 8. No endpoint ownership model exists

An ADC sensor, GPIO, PWM output, servo and RGB light can all target the same
physical signal. The firmware lets the most recent command silently change its
mode, producing intermittent behavior that looks like a transport problem.

v2 action: claim endpoints during validation/code generation and again in C++ as
a defensive check. A conflict identifies both owners and fails loudly.

### 9. Feature guards and class layout are fragile

Output headers are effectively included unconditionally, dummy fallback classes
exist when a feature is disabled, and some method implementations are outside the
same feature guards as their declarations. A hub-only configuration can compile
different code than intended.

v2 action: keep the core transport independent and compile entity wrappers only
under their matching `USE_*` guards. Remove warning directives and dummy feature
classes.

### 10. Setup does not establish device health

Setup currently logs an address but does not verify that the hub responds or
cache firmware capabilities. Repeated read failures can also flood warnings.

v2 action: require a successful application-version read of exactly `2`, report a
useful component state, count consecutive failures, throttle repeated logs and
clear warnings after recovery.

### 11. Documentation disagrees with the actual component

The README uses an incorrect default address in examples, names platform values
that do not match the Python module layout, and presents PWM as suitable for
RTTTL and direct ESPHome servo use.

v2 action: regenerate every example from a fixture that passes the target
ESPHome configuration validator, then explain the application-firmware-v2 limits
next to the relevant example.

## Planned public YAML API

Names below are the design target. They remain subject to configuration and
compile tests during implementation.

### Hub

```yaml
pbhub:
  id: hub
  i2c_id: i2c_bus
  address: 0x61
```

Optional LED timing mode belongs on the hub because register `0xFA` is global:

```yaml
pbhub:
  id: hub
  i2c_id: i2c_bus
  address: 0x61
  led_timing_mode: 0
```

If omitted, v2 leaves the firmware default untouched. If configured, the value
must be 0 or 1 and the hub must report application firmware version exactly 2.

### Digital input

```yaml
binary_sensor:
  - platform: pbhub
    pbhub_id: hub
    pin: 31
    update_interval: 100ms
    inverted: true
    name: Door
```

The sensor publishes only after a successful I2C read. On failure it retains the
last state and contributes to the parent's communication warning.

### Digital output

```yaml
switch:
  - platform: pbhub
    pbhub_id: hub
    pin: 41
    inverted: false
    restore_mode: ALWAYS_OFF
    name: Relay
```

The default must be safe-off. Restoring any other state is explicit and occurs
only after the hub is available.

### ADC

```yaml
sensor:
  - platform: pbhub
    pbhub_id: hub
    slot: 3
    update_interval: 1s
    name: Analog Input
```

The sensor publishes the raw integer range `0..4095` without pretending to know
the connected sensor's voltage or engineering-unit conversion. Users can apply
ESPHome filters.

### Fixed-frequency PWM

```yaml
output:
  - platform: pbhub
    id: hub_pwm
    pbhub_id: hub
    pin: 11
    mode: pwm
```

`mode: pwm` converts `0.0..1.0` to a rounded and clamped duty byte `0..255`.
Frequency remains fixed by the STM32 firmware at a calculated nominal 392.16 Hz.

### Servo

```yaml
output:
  - platform: pbhub
    id: hub_servo_output
    pbhub_id: hub
    pin: 11
    mode: servo
    zero_means_zero: true

servo:
  - id: hub_servo
    output: hub_servo_output
    min_level: 2.5%
    idle_level: 7.5%
    max_level: 12.5%
```

ESPHome's servo component expresses the pulse as a fraction of its 20 ms frame.
The PBHUB output converts that fraction to microseconds and uses the direct servo
pulse register. A zero output uses a digital-low command to detach because zero
is not a valid servo pulse in the STM32 protocol. The percentages above map to
500, 1500 and 2500 microseconds in a 20 ms frame. Servo mode forces a neutral
`FloatOutput` transform: no inversion or power remapping, and zero must remain
zero. Calibration belongs in the `servo` component's level settings.

### Uniform RGB strip

```yaml
light:
  - platform: pbhub
    pbhub_id: hub
    slot: 3
    num_leds: 12
    name: Hub LEDs
```

This is one uniform RGB light spanning the configured LEDs. It is not an
addressable light. Setup writes LED count and firmware brightness 255. Updates
host-scale on/off, brightness and RGB, then issue one safe fill from index 0 for
`num_leds` entries.

## Support boundary

v2 is a deliberate clean break from the existing component. Correctness and a
clear API take precedence over preserving prototype behavior.

1. The only device contract is the stock PBHUB application protocol reporting
   firmware version `2` from register `0xFE`.
2. The only software target is ESPHome 2026.7.0. Use its public APIs directly;
   do not add cross-version shims or version conditionals.
3. Require an explicit `mode` for every PBHUB output. Do not infer PWM or servo
   behavior from an old configuration shape.
4. Do not accept, normalize or deprecate old PBHUB YAML shapes. There are no
   legacy aliases or transition windows.
5. Remove the custom GPIOPin adapter instead of retaining an output-only form.
   Digital input and output use the native PBHUB binary-sensor and switch
   platforms; PWM and servo use the new output platform.
6. Invalid endpoint values, channel-0 fallback, pseudo-servo through PWM and
   misleading RTTTL behavior fail or remain unsupported without compatibility
   exceptions.
7. Rewrite and validate the representative deployment directly against the new
   API without copying its names, identifiers, secrets or paths into this
   repository.
8. Publish the final v2 YAML reference and a concise clean-break notice. No
   migration layer or compatibility promise is provided for pre-v2 configs.

## Python/schema architecture

### Shared validators

Create one shared module-level implementation for:

- `validate_slot`: integer `0..5`.
- `validate_endpoint`: split with `divmod(value, 10)` and require channel `0..5`
  plus index `0 or 1`.
- `validate_led_count`: integer `1..74`.
- required output mode: exact enum `pwm` or `servo`.
- servo output transform: require `inverted: false`, `min_power: 0%`,
  `max_power: 100%` and `zero_means_zero: true`; reject incompatible values.
- LED timing mode: exact enum/integer 0 or 1.

Error text should include the endpoint formula and accepted values. Platform
schemas must import these validators instead of recreating loose ranges.

### Parent references

Use one canonical `pbhub_id` field for every platform. No aliases are accepted,
so Python validation and C++ code generation share one representation.

The root component schema should extend the standard I2C device schema once;
it should not define `address` twice. `MULTI_CONF` remains supported.

### Ownership validation

Each entity declares a claim:

| Entity | Claimed endpoint |
|---|---|
| Digital input/switch/PWM/servo | Configured endpoint |
| ADC sensor | `slot * 10 + 0` |
| RGB light | `slot * 10 + 1` |

Use ESPHome final validation to compare claims belonging to the same hub ID. The
error must report the endpoint and both conflicting config paths. Add a matching
C++ `claim_endpoint()` check so lambdas or future codegen mistakes cannot create
a silent runtime conflict.

Use the ESPHome 2026.7.0 final-validation API directly. Keep the matching C++
guard as defense in depth, not as a fallback for another ESPHome release.

## C++ architecture

### File responsibilities

Target layout:

```text
components/pbhub/
  __init__.py
  binary_sensor.py
  light.py
  output.py
  sensor.py
  switch.py
  pbhub_protocol.h
  pbhub.h
  pbhub.cpp
  pbhub_entities.h
  pbhub_entities.cpp
```

- `pbhub_protocol.h`: scoped constants, `Endpoint`, register construction and
  endian helpers with no ESPHome entity dependency.
- `pbhub.h/.cpp`: I2C transport, device health, verified firmware-version state,
  endpoint ownership and typed protocol operations.
- `pbhub_entities.h/.cpp`: thin feature-guarded ESPHome entity wrappers.
- Python files: schemas, ownership validation and code generation.

The exact split can be reduced if it creates unnecessary indirection, but core
transport must not depend on `USE_OUTPUT`, `USE_SENSOR`, `USE_LIGHT`,
`USE_BINARY_SENSOR` or `USE_SWITCH`.

### Protocol types and register construction

Replace preprocessor register macros with scoped `constexpr` values. Use an
exact six-element channel base table:

```cpp
struct Endpoint {
  uint8_t channel;
  uint8_t index;
};
```

Register construction must return success/failure or operate only on a validated
`Endpoint`. It must never substitute channel 0.

Central typed operations should have signatures equivalent to:

```cpp
bool read_digital(Endpoint endpoint, bool &value);
bool write_digital(Endpoint endpoint, bool value);
bool read_adc(uint8_t slot, uint16_t &value);
bool write_pwm(Endpoint endpoint, uint8_t duty);
bool write_servo_pulse(Endpoint endpoint, uint16_t pulse_us);
bool configure_leds(uint8_t slot, uint16_t count);
bool fill_leds(uint8_t slot, uint16_t start, uint16_t count,
               uint8_t red, uint8_t green, uint8_t blue);
```

Out-parameters ensure a valid zero/false remains distinct from failure. Every
public method validates again at runtime before constructing a register.

This success/failure boundary covers host-visible transport results. It cannot
detect the firmware's stale-response ADC timeout when I2C itself succeeds.

### ESPHome 2026.7.0 I2C transport

Use the current three-argument `read_register()` and `write_register()` APIs and
handle their `i2c::ErrorCode` results directly. No compatibility wrapper or
version conditional is required.

Every read selects its register and requests exactly the expected response size.
No method performs a bare continuation read. Every multi-byte value is assembled
explicitly as little-endian.

### Device health

The parent owns communication health:

- count consecutive failures;
- log the first failure with operation/register/error context;
- throttle repeated messages;
- set a component warning after a small threshold, initially three failures;
- clear the counter and warning on a successful transaction;
- expose total/recent failure counters to verbose logs, not as mandatory Home
  Assistant entities;
- never mark an entity's cached output successful until its I2C write succeeds.

Setup and recovery probe register `0xFE`. Until a successful response reports
version `2`, the parent suppresses feature commands and retries after transport
failures. A successfully read value other than `2` is unsupported and marks the
component failed with a clear diagnostic. There is no firmware-v1 fallback.

### Output caching and traffic control

Cache the last successfully written digital, PWM, servo and RGB values. Skip an
identical write only after a confirmed prior success. A failed write leaves the
cache unchanged so the same desired value can retry.

Scheduled input/ADC reads should be serialized by the parent and, where practical,
staggered so several equal polling intervals do not create one burst. Start with
at most one scheduled read per parent loop pass; measure before adding a more
complex queue. User-triggered output changes remain immediate.

RGB updates should be coalesced to the final state available in a loop pass and
rate-limited if effects or rapid transitions generate faster changes than the
firmware can transmit safely.

### Feature guards

Entity declarations and implementations must use matching `USE_*` guards. There
are no dummy fallback classes and no compile-time warnings for absent optional
features. No GPIOPin compatibility adapter or `dump_summary` signature shim
remains.

## Entity behavior contracts

### Binary sensor

- Default polling interval: 100 ms, configurable.
- Read configures the selected signal as a floating input, as required by the
  firmware.
- Inversion applies only after a successful raw read.
- Failure preserves the published state.
- No internal pull-up/down options are advertised.

### Switch

- Default restore mode is safe-off.
- Inversion is applied before transport.
- A successful write publishes the requested logical state.
- A failed write leaves state unchanged and marks communication health.

### ADC sensor

- Slot only, signal A implicit.
- Raw range `0..4095`, zero accuracy decimals, no invented unit.
- Default update interval: 1 s, configurable.
- A detected I2C failure preserves last state.
- Documentation states that a successful read can still be stale after the
  source-confirmed firmware ADC timeout; v2 cannot guarantee sample freshness.
- Documentation warns that the signal is 3.3 V logic/ADC and must not be driven
  above the documented range.

### PWM output

- Clamp input to `0.0..1.0`, round to `0..255`.
- Skip a repeated successful value.
- Advertise fixed calculated nominal frequency, not a configurable frequency.
- Override ESPHome 2026.7.0's `update_frequency(float)` hook, leave the fixed
  frequency unchanged and emit one throttled warning. Do not claim RTTTL support.

### Servo output

- Convert a nonzero frame fraction to rounded microseconds using a 20,000 us
  frame.
- Force `zero_means_zero: true` and reject inversion or non-neutral
  `min_power`/`max_power` settings in `mode: servo`. Dynamic output power-scaling
  actions are unsupported; servo calibration uses `min_level`, `idle_level` and
  `max_level` on the ESPHome servo component.
- Accept only `500..2500 us`; reject other nonzero values locally.
- A zero level sends digital low to detach and resets the host cache accordingly.
- Use the direct pulse register, not angle or PWM duty.
- Document firmware jitter and require load testing before safety-sensitive use.

### RGB light

- Validate slot `0..5` and count `1..74`.
- During setup: set count, set firmware brightness 255, optionally set the global
  timing mode once, then send black if restoration policy requires it.
- On update: resolve on/off, brightness and RGB on the host; send one bounded fill
  at start 0 for exactly `num_leds`.
- Cache the final scaled RGB triple after successful transport.
- State clearly that all configured LEDs share one color.

## Implementation phases and acceptance criteria

### Phase 0 - Research and design

Status: complete on the `v2` branch when these documents are reviewed.

Deliverables:

- Firmware protocol datasheet pinned to an upstream source commit.
- Source-versus-published differences and known defects.
- This phased implementation plan.

Acceptance:

- No component behavior changed.
- Public documents contain no machine-local paths, deployment identifiers or
  workflow history.

### Phase 1 - Validation and test scaffold

Changes:

- Add exact slot/endpoint/count/mode validators.
- Add positive and negative YAML fixtures under `tests/`.
- Add a non-hidden local runner under `tools/`.
- Pin the local validation environment to ESPHome 2026.7.0 without committing
  machine-local environment paths.

Acceptance:

- Every valid endpoint passes.
- Invalid in-range-looking values such as 2, 19 and 49 fail with useful errors.
- Invalid slots, counts and modes fail.
- Core-only fixture compiles without optional entity domains.

### Phase 2 - Core protocol and transport rewrite

Changes:

- Introduce protocol types and exact register construction.
- Replace value-or-error conflation with boolean success plus out-parameters.
- Add firmware probing, health counters, warning recovery and log throttling.
- Add endpoint ownership claims.
- Remove unsafe base fallback and duplicate schema fields.

Acceptance:

- Unit-level register tests cover all six channel bases and all operations.
- No invalid endpoint can produce an I2C register.
- Simulated host-detected transport failure does not become a valid reading.
- A simulated firmware-version response of 2 enables operation; any other
  successful version response is rejected without issuing feature commands.
- An unreadable version is treated as a recoverable transport failure and is
  retried without issuing feature commands.
- Core-only and multi-hub fixtures configure and compile.

### Phase 3 - Native digital entities

Changes:

- Add native polling binary sensor.
- Add native digital switch.
- Remove the custom GPIOPin adapter.
- Validate a representative deployment configuration without tracking it.

Acceptance:

- Input failure never flips an inverted sensor to true.
- Polling frequency matches configuration and does not run every main-loop pass.
- Switch only publishes successful writes.
- Duplicate digital ownership fails configuration.

### Phase 4 - ADC and fixed PWM

Changes:

- Replace pin-shaped ADC schema with slot.
- Publish raw ADC only on successful reads.
- Rebuild PWM conversion, caching and fixed-frequency reporting.

Acceptance:

- All six ADC slots produce the correct register and read two little-endian bytes.
- Valid ADC zero publishes as zero; I2C failure does not.
- PWM edge values map predictably to bytes 0 and 255.
- RTTTL is no longer shown as supported.

### Phase 5 - Servo mode

Changes:

- Add `mode: servo` to the output platform.
- Convert standard ESPHome servo frame fractions to microseconds.
- Implement digital-low detach.

Acceptance:

- Minimum, center and maximum standard pulses encode as 500, 1500 and 2500 us.
- Zero detaches without sending an invalid servo pulse.
- Invalid nonzero pulse fractions fail locally and do not change hub mode.
- Inverted or power-remapped servo output configurations fail validation, and
  zero remains a detach command.

### Phase 6 - RGB rebuild

Changes:

- Correct slot/count validation and initialization.
- Apply host brightness and uniform fill.
- Add optional firmware-v2 LED timing mode at parent scope.
- Coalesce redundant writes.

Acceptance:

- Counts 1 and 74 pass; 0 and 75 fail.
- Every generated fill satisfies `start + count <= configured_count <= 74`.
- Brightness never uses the STM32's defective scaler below 255.
- The light turns fully off with an explicit black fill.

### Phase 7 - Documentation and ESPHome 2026.7.0 validation

Changes:

- Rewrite README installation and all entity examples.
- Publish a concise clean-break notice and the complete new YAML reference.
- Document firmware version, PWM, servo, RGB and voltage limitations.
- Run the complete framework/target matrix under ESPHome 2026.7.0.

Acceptance:

- Every public example is a validated test fixture or is generated from one.
- No README platform names or addresses disagree with the schemas.
- Every applicable example configures and compiles with ESPHome 2026.7.0.
- Privacy scan is clean.

### Phase 8 - Hardware validation and release

Changes:

- Execute the hardware matrix below.
- Record measured values separately from calculated firmware values.
- Fix host-side issues exposed by measurement.
- Tag a v2 prerelease, validate a representative deployment, then prepare
  merge/release.

Acceptance:

- No unresolved high-risk host defect.
- PWM/servo limitations are measured and documented.
- Disconnect/reconnect behavior does not publish false states.
- The representative deployment runs successfully on the clean v2 API.
- Main remains untouched until the v2 branch is explicitly approved for merge.

## Repository validation matrix

### Target ESPHome version

The sole target is ESPHome 2026.7.0. All configuration validation, code
generation and compilation runs use that exact version. Supporting another
ESPHome release is separate future work and requires an explicit plan update.

### Frameworks and targets

- ESP32 with Arduino.
- ESP32 with ESP-IDF.
- ESP8266 with Arduino.
- At least one ESP32-S3 fixture matching the primary hardware target.
- Direct I2C and a fixture using a compatible I2C multiplexer channel.
- Multiple hubs on separate buses and, where addresses differ, one bus.

### Positive fixtures

- Hub only.
- Native digital input and switch.
- All twelve valid endpoints.
- ADC on all six slots.
- PWM mode.
- Servo mode through ESPHome servo.
- RGB counts 1 and 74.
- Multiple hubs and buses.

### Negative fixtures

- Invalid endpoints including 2, 9, 12, 19, 42 and 49.
- Slots below 0 or above 5.
- RGB counts 0 and 75.
- Unknown output and LED timing modes.
- ADC configured with an endpoint instead of a slot.
- Servo inversion, non-neutral power scaling and `zero_means_zero: false`.
- Two features claiming the same endpoint.

### Checks

The local validation runner should perform, as applicable:

1. Python/schema fixture validation.
2. `esphome config` for positive fixtures.
3. Expected-failure assertions for negative fixtures.
4. Compile each framework/target fixture under ESPHome 2026.7.0.
5. C++ formatting/static checks available without tracked dotfiles.
6. `git diff --check`.
7. Privacy scan for absolute home paths, deployment identifiers, hidden workflow
   references and secret-like values.

Because hidden files are deliberately excluded, do not add `.github`, formatter
dotfiles or local workflow notes. A hosted CI service can be revisited only if the
repository policy changes explicitly.

## Required real-hardware matrix

### Digital

- Read and write signal A and B on all six channels.
- Verify inversion and safe startup output state.
- Disconnect and reconnect the hub while polling; confirm last state is retained
  during failure and communication warning clears on recovery.
- Measure the shortest reliably detected input pulse at several polling intervals.

### ADC

- Apply safe known voltages including zero to signal A on every channel.
- Compare raw readings, noise and trimmed averaging.
- Confirm valid zero remains distinguishable from I2C failure.
- Confirm ADC polling changes only the expected signal's active mode.

### PWM

- Measure duty values 0, 1, 127, 254 and 255.
- Measure nominal frequency and period variation.
- Look specifically for a duty-zero high glitch.
- Exercise PWM re-entry after digital, ADC, servo and RGB mode changes.
- Test the source-confirmed stale-state/duty-255 sequence.

### Servo

- Measure pulse widths at 500, 1500 and 2500 us and the frame period.
- Verify detach drives low and stops pulses.
- Test first-frame behavior after mode changes.
- Repeat while ADC reads and a 74-LED RGB update are active.

### RGB

- Verify R/G/B order, off and host-side low/mid/full brightness.
- Test counts 1, an intermediate value and 74.
- Test timing modes 0 and 1 on representative LED families.
- Confirm no command generated by the component can exceed the safe fill bound.

### Bus and power

- Test 100 kHz and 400 kHz I2C.
- Test direct bus and multiplexer routing.
- Test controller-first, hub-first and simultaneous power-up.
- Test hub reset and reconnect without rebooting the ESP controller.
- Observe PWM/servo outputs during induced I2C errors and recovery.

## Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| Clean rewrite requires replacing the existing YAML | One-time deployment update | Rewrite the representative configuration directly; add no aliases or shims |
| Input polling still worsens timing | PWM/servo jitter | Explicit intervals, staggering, one scheduled read per loop pass, measurement |
| RGB traffic blocks software outputs | Long pulse or visible flicker | Uniform/coalesced fills, rate limit, hardware stress test |
| Firmware invalid-value side effects | Unexpected output | Validate every range before I2C write |
| Hub disconnect becomes a false alarm | Unsafe automation | Preserve state on read failure and expose parent warning |
| Restore mode energizes an output | Physical hazard | Safe-off default and explicit opt-in restoration |
| Public docs leak machine-local context | Privacy/repository hygiene failure | Portable examples and automated privacy scan |

## v2 completion definition

The overhaul is complete only when all of the following are true:

- Protocol helpers cannot encode an invalid endpoint or unsafe RGB range.
- No feature command is sent before application firmware version `2` is
  verified.
- No legacy PBHUB schema, alias, GPIOPin adapter or compatibility API remains.
- Every entity keeps host-detected transport failure separate from valid state;
  the undetectable firmware ADC-staleness case is documented explicitly.
- Endpoint conflicts fail before normal operation.
- Digital, ADC, PWM, servo and uniform RGB behavior matches the datasheet.
- Public examples validate and compile under ESPHome 2026.7.0 for every claimed
  framework/target combination.
- Required hardware tests are recorded, with measured and calculated behavior
  clearly separated.
- A representative deployment using only the clean v2 API runs through a
  meaningful soak period.
- README and the clean-break release note state stock-firmware limitations
  plainly.
- Repository privacy and dotfile policy remain intact.
- No reset, firmware-flashing or runtime address-mutation code is present.
