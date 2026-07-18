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
6. Target the audited stock PBHUB application protocol reporting firmware
   version 2.
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
| PWM | `output` mode with fixed frequency near 392 Hz; use digital low/high for encoded duties 0/255 | Firmware version 2 has no frequency control, and its PWM extrema have edge-state defects |
| Servo | Separate `output` mode using the direct pulse register | Application firmware version 2 has a genuine 50 Hz servo generator; ordinary PWM is not equivalent |
| RGB | One uniform RGB light per configured channel/strip | Every indexed command immediately transmits a strip prefix, making addressable effects inefficient |
| RGB brightness | Scale on the ESP host; keep STM32 brightness at 255 | Firmware brightness is nonlinear and applies one color write late |
| Endpoint conflicts | Reject duplicate ownership | Firmware commands silently replace the current mode of a physical signal |
| Dangerous globals | Excluded | Address mutation wears flash, and reset/IAP is outside the ESPHome component's scope |
| Firmware target | Audited stock protocol reporting application version exactly 2 | The version register is a compatibility guard, not authentication of the exact factory binary |
| Transport API | ESPHome 2026.7.0 `read_register`/`write_register`, returning `i2c::ErrorCode` | These current APIs directly match the PBHUB register transaction model |
| ESPHome target | Exactly 2026.7.0 | No cross-version shims or historical API branches are required |
| Reported output state | Switch publishes after successful transport; light reports ESPHome desired state | ESPHome light state is published before `LightOutput::write_state()`, and firmware has no physical feedback |
| Recovery | Invalidate verification and applied-state caches after transport loss; rate-limit re-probe and replay | Only the I2C address persists across a hub reset |

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
can appear updated even when the command was not transported successfully.
Conversely, an I2C success still does not prove semantic acceptance or the
physical output level because firmware v2 has no such acknowledgement.

v2 action: centralize result handling and keep desired state separate from the
last successfully transported command. Publish switch state only after transport
success. ESPHome's light entity necessarily remains desired logical state because
it publishes before `LightOutput::write_state()`; transport failures preserve
that desired state for replay and surface through parent communication health.
Neither entity is physical feedback.

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

### 10. Setup and recovery do not establish or restore device state

Setup currently logs an address but does not verify that the hub responds or
cache firmware capabilities. Repeated read failures can also flood warnings. A
hub reset loses every configured mode and value except the I2C address, while
host-side caches can still make those values look applied.

v2 action: require a successful application-version read of exactly `2`, report
a useful component state, count consecutive failures and throttle repeated logs.
On transport loss, invalidate firmware verification and every applied-state
cache. After a rate-limited successful re-probe, restore configured global and
RGB state, then replay desired entity outputs before clearing the warning.

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
That response selects the audited protocol contract; it does not authenticate
the exact factory binary.

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
only after the hub is available. Published state represents the last
successfully transported command, not physical output feedback.

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
Encoded duty 0 uses a digital-low command, encoded duty 255 uses digital high,
and only 1 through 254 use the PWM register. Frequency for intermediate levels
remains fixed by the STM32 firmware at a calculated nominal 392.16 Hz.

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
addressable light. Setup writes LED count and firmware brightness 255. Each
update resolves on/off, brightness and RGB on the host, then issues one safe fill
from index 0 across exactly `num_leds` entries. Detected transport recovery
replays count and firmware brightness before the desired light state.

## Support boundary

v2 is a deliberate clean break from the existing component. Correctness and a
clear API take precedence over preserving prototype behavior.

1. The only device contract is the audited stock PBHUB application protocol
   reporting firmware version `2` from register `0xFE`. This self-reported byte
   does not authenticate the exact factory image or modified firmware that keeps
   the same value.
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
- required output mode: exact enum `pwm` during Phase 1, extended to `pwm` or
  `servo` when direct-pulse servo support lands in Phase 5.
- servo output transform, added with Phase 5: require `inverted: false`,
  `min_power: 0%`, `max_power: 100%` and `zero_means_zero: true`; reject
  incompatible values.
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
  pbhub_ownership.h
  pbhub_recovery.h
  pbhub.h
  pbhub.cpp
  pbhub_entities.h
  pbhub_entities.cpp
```

- `pbhub_protocol.h`: scoped constants, `Endpoint`, register construction and
  endian helpers with no ESPHome entity dependency.
- `pbhub_ownership.h`: host-testable endpoint ownership registry with no
  ESPHome entity dependency.
- `pbhub_recovery.h`: host-testable health state machine and recovery-client
  orchestration with no ESPHome entity dependency.
- `pbhub.h/.cpp`: I2C transport, device health, verified firmware-version state,
  recovery replay, endpoint ownership and typed protocol operations.
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
bool write_servo_detach(Endpoint endpoint);
bool configure_leds(uint8_t slot, uint16_t count);
bool set_led_full_brightness(uint8_t slot);
bool fill_leds(uint8_t slot, uint16_t configured_count,
               uint16_t start, uint16_t count,
               uint8_t red, uint8_t green, uint8_t blue);
```

Out-parameters ensure a valid zero/false remains distinct from failure. Every
public method validates again at runtime before constructing a register.

For writes, `true` means that the exact validated command was transported
without an ESPHome I2C error; it is not physical-state confirmation. The
firmware-version read and optional global LED timing write are also typed
protocol commands even though they are parent-internal rather than public entity
operations. The `write_pwm()` operation selects digital low/high for duties
0/255 internally. `fill_leds()` requires the configured strip count so it can
prove `start + count <= configured_count <= 74` before constructing a register.
This success/failure boundary cannot detect the firmware's stale-response ADC
timeout when I2C itself succeeds.

### ESPHome 2026.7.0 I2C transport

Use the current three-argument `read_register()` and `write_register()` APIs and
handle their `i2c::ErrorCode` results directly. No compatibility wrapper or
version conditional is required.

Every read selects its register and requests exactly the expected response size.
No method performs a bare continuation read. Every multi-byte value is assembled
explicitly as little-endian. Protocol tests assert the exact write payload and
read response length of every typed operation because firmware length handling
is inconsistent.

### Device health

The parent owns communication health:

- count consecutive failures;
- log the first failure with operation/register/error context;
- throttle repeated messages;
- set a component warning immediately when a `READY` parent loses transport;
- clear the consecutive-failure counter on a successful transaction and clear
  the warning only after any required recovery replay completes;
- expose total/recent failure counters to verbose logs, not as mandatory Home
  Assistant entities;
- treat I2C success as successful command transport, not semantic acceptance or
  physical-state confirmation;
- never mark an applied-state cache current until its I2C write succeeds.

Setup and recovery probe register `0xFE`. Until a successful response reports
version `2`, the parent suppresses feature commands. A successfully read value
other than `2` is unsupported and marks the component failed with a clear
diagnostic. A value of `2` is a self-reported protocol guard, not authentication
of the exact stock binary. There is no firmware-v1 fallback.

The first transport failure makes hub state uncertain. The parent immediately
invalidates firmware verification and every applied-state cache, retains desired
entity state, and schedules a rate-limited version probe rather than retrying on
every loop pass. After `0xFE` again reports 2, recovery proceeds in this order:

1. restore configured global LED timing, if present;
2. restore each RGB light's LED count and firmware brightness 255;
3. replay the desired state of switches, PWM, servo and RGB entities; and
4. resume scheduled reads and clear the warning after recovery completes.

Only the I2C address persists across a hub reset. A reset that starts and
finishes entirely between host transactions can be invisible because firmware
exposes no reset counter. Version probing cannot detect that case when the hub
returns reporting 2; documentation must state this irreducible limitation.

### Recovery orchestration

Represent parent health with explicit `UNVERIFIED`, `RECOVERING`, `READY` and
`UNSUPPORTED` states. Startup begins unverified. A transport failure from any
operation returns the parent to `UNVERIFIED`; an unsupported version is terminal
until the controller restarts.

Entity wrappers register through a small core recovery-client interface that has
no dependency on ESPHome entity domains. The interface lets the parent:

- invalidate a client's applied-state cache while preserving desired state;
- restore configuration such as RGB count and firmware brightness; and
- replay desired output state after all configuration is restored.

Code generation registers every client before ESPHome calls component setup. The
parent uses `setup_priority::IO`, after the I2C bus and before hardware-facing
entity setup. During initial setup it probes the version, applies configured
global timing and runs the client configuration pass. Each entity then records
its initial desired state and applies it only if the parent is `READY`. If the
probe or configuration failed, entities retain desired state without issuing
feature commands and the normal loop-driven recovery sequence performs the first
application later.

The parent writes its optional global LED timing first, runs one configuration
pass across clients, then one output-state pass. A failure during any pass stops
recovery, invalidates applied state again and schedules another bounded retry.
No recovery loop performs immediate unbounded retries. Normal scheduled reads
and user-triggered feature commands run only in `READY`; commands received while
recovering update desired state and wait for replay.

Local argument-validation failures never enter transport recovery because they
do not make the hub's existing state uncertain.

### Output caching and traffic control

Keep desired state separate from the last successfully transported digital, PWM,
servo and RGB command. Every applied-state cache starts unknown. Skip an
identical write only while the corresponding applied-state cache is known. A
failed write retains desired state, makes applied state unknown and enters the
parent recovery path so that the command can replay.

The PWM typed operation accepts the encoded duty byte but sends digital low for
0, digital high for 255 and the firmware PWM register only for 1 through 254.
The applied cache includes the effective command mode so transitions between a
digital extremum and intermediate PWM are never incorrectly skipped.

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
- ESPHome applies inversion before `write_state(bool)`. Store and replay that
  transport-level boolean, then pass the same value to `publish_state()` after a
  successful write so inversion is not applied twice.
- A successfully transported write publishes the requested logical state as
  commanded state, not confirmed physical feedback.
- A failed write leaves state unchanged and marks communication health.
- Keep the normal optimistic toggle UI (`assumed_state() == false`); the
  documented entity contract is commanded state even though no physical
  readback exists.

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
- Send encoded 0 as digital low, encoded 255 as digital high and only 1 through
  254 through the PWM register.
- Cache both encoded value and effective command mode; skip only a repeated,
  successfully transported value while applied state remains known.
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
- Keep the desired final light state separately and cache the scaled RGB triple
  only as the last successfully transported fill.
- ESPHome publishes the light's desired logical state before this output writes;
  on failure retain it for replay and rely on the parent warning rather than
  claiming that the light state confirms transport or hardware.
- State clearly that all configured LEDs share one color.

## Implementation phases and acceptance criteria

### Phase 0 - Research and design

Status: complete on the `v2` branch after the final firmware-audit resync.

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

- Add exact slot, endpoint and count validators.
- Require an explicit `mode: pwm` on the output platform. Reject a premature
  `mode: servo` until its direct-pulse implementation lands in Phase 5.
- Add positive and negative YAML fixtures under `tests/`.
- Add a non-hidden local runner under `tools/`.
- Pin the local validation environment to ESPHome 2026.7.0 without committing
  machine-local environment paths.

Acceptance:

- Every valid endpoint passes.
- Invalid in-range-looking values such as 2, 19 and 49 fail with useful errors.
- Invalid slots and counts, a missing or unknown output mode and premature
  `mode: servo` fail with useful errors.
- Pre-v2 nested output, ADC and RGB configuration shapes fail without aliases.
- The core-only fixture compiles without optional entity domains, and each
  Phase 1 PWM, ADC and RGB validation fixture also compiles.

### Phase 2 - Core protocol and transport rewrite

Changes:

- Introduce protocol types and exact register construction.
- Replace value-or-error conflation with boolean success plus out-parameters.
- Add the parent health state machine, recovery-client interface, firmware
  probing, health counters, rate-limited retries and applied-state invalidation.
- Add endpoint ownership claims.
- Remove the obsolete custom GPIOPin adapter so it cannot bypass cross-platform
  ownership validation.
- Remove unsafe base fallback and duplicate schema fields.

Acceptance:

- Unit-level register tests cover all six channel bases, every exposed typed
  operation, the firmware/global commands and the exact payload/response length
  of every transaction.
- No invalid endpoint can produce an I2C register.
- Simulated host-detected transport failure does not become a valid reading.
- A simulated firmware-version response of 2 enables operation; any other
  successful version response is rejected without issuing feature commands.
- An unreadable version is treated as a recoverable transport failure and is
  retried at a bounded interval without issuing feature commands.
- A transport failure invalidates verification and all applied-state caches.
- Fake recovery-client tests prove that successful re-probing runs all
  configuration callbacks before output-state callbacks and that a failed
  callback returns to bounded `UNVERIFIED` recovery without normal feature
  traffic.
- Initial-order tests prove the `setup_priority::IO` parent probes and completes
  its configuration pass before an entity attempts its initial output state.
- Core-only and multi-hub fixtures configure and compile.

### Phase 3 - Native digital entities

Changes:

- Add native polling binary sensor.
- Add native digital switch.
- Validate a representative deployment configuration without tracking it.

Acceptance:

- Input failure never flips an inverted sensor to true.
- Polling frequency matches configuration and does not run every main-loop pass.
- Switch publishes only successfully transported writes and retains a failed
  request as desired state for recovery replay.
- Detected recovery replays a switch's desired state before publishing it.
- Normal and inverted switches transport/replay the correct raw boolean and
  publish the correct logical state without double inversion.
- Duplicate digital ownership fails configuration.

### Phase 4 - ADC and fixed PWM

Changes:

- Replace pin-shaped ADC schema with slot.
- Publish raw ADC only on successful reads.
- Rebuild PWM conversion, mode-aware caching and fixed-frequency reporting.

Acceptance:

- All six ADC slots produce the correct register and read two little-endian bytes.
- Valid ADC zero publishes as zero; I2C failure does not.
- PWM encoded 0 sends digital low, encoded 255 sends digital high and values 1
  through 254 use the correct PWM register and duty byte.
- Transitions between a digital extremum and intermediate PWM are not skipped by
  the applied-state cache.
- Detected recovery replays the desired encoded PWM level using the same
  digital-extrema rule.
- RTTTL is no longer shown as supported.

### Phase 5 - Servo mode

Changes:

- Extend the output-mode validator and code generation with `mode: servo`.
- Convert standard ESPHome servo frame fractions to microseconds.
- Implement digital-low detach.

Acceptance:

- Minimum, center and maximum standard pulses encode as 500, 1500 and 2500 us.
- Zero detaches without sending an invalid servo pulse.
- Invalid nonzero pulse fractions fail locally and do not change hub mode.
- Inverted or power-remapped servo output configurations fail validation, and
  zero remains a detach command.
- Detected recovery replays the desired servo level or detach state.

### Phase 6 - RGB rebuild

Changes:

- Correct slot/count validation and initialization.
- Apply host brightness and uniform fill.
- Add optional firmware-v2 LED timing mode at parent scope.
- Coalesce redundant writes.

Acceptance:

- Counts 1 and 74 pass; 0 and 75 fail.
- Every generated fill satisfies `start + count <= configured_count <= 74`.
- An invalid range fails locally and produces no I2C transaction.
- Brightness never uses the STM32's defective scaler below 255.
- Host-scaled RGB byte boundaries 0, 127, 128, 254 and 255 are encoded exactly.
- The light turns fully off with an explicit black fill.
- Detected recovery restores timing/count/brightness before replaying the
  desired fill; a failed step does not advance to output replay.

### Phase 7 - Documentation and ESPHome 2026.7.0 validation

Changes:

- Rewrite README installation and all entity examples.
- Publish a concise clean-break notice and the complete new YAML reference.
- Document the self-reported firmware-version guard, commanded-state semantics,
  undetectable between-transaction reset, PWM, servo, RGB and voltage limits.
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
- Disconnect/reconnect behavior does not publish false readings and restores
  desired outputs after verified recovery.
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
  during failure, desired output state replays only after version verification
  and the communication warning clears after recovery completes.
- Measure the shortest reliably detected input pulse at several polling intervals.

### ADC

- Apply safe known voltages including zero to signal A on every channel.
- Compare raw readings, noise and trimmed averaging.
- Confirm valid zero remains distinguishable from I2C failure.
- Confirm ADC polling changes only the expected signal's active mode.

### PWM

- Verify encoded duties 0 and 255 use stable digital low and high without
  writing the PWM registers.
- Measure nominal frequency, duty and period variation at encoded values 1, 127
  and 254.
- Exercise PWM re-entry after digital extrema, ADC, servo and RGB mode changes,
  looking for a missing or stretched first pulse.

### Servo

- Measure pulse widths at 500, 1500 and 2500 us and the frame period.
- Verify detach drives low and stops pulses.
- Test first-frame behavior after mode changes.
- Repeat while ADC reads and a 74-LED RGB update are active.

### RGB

- Verify R/G/B order, off and host-side scaling at encoded byte values 0, 127,
  128, 254 and 255.
- Test counts 1, an intermediate value and 74.
- Test timing modes 0 and 1 on representative LED families.
- Confirm no command generated by the component can exceed the safe fill bound.
- Confirm rejected indices/ranges produce no I2C transaction and do not take
  signal B away from its configured owner.

### Bus and power

- Test 100 kHz and 400 kHz I2C.
- Test direct bus and multiplexer routing.
- Test controller-first, hub-first and simultaneous power-up.
- Interrupt a transaction, induce bus errors and look specifically for repeated
  firmware-side 500 ms recovery stalls.
- Test hub reset and reconnect without rebooting the ESP controller; verify
  rate-limited re-probing, configuration ordering and desired-state replay.
- Reset an output-only hub entirely between host transactions and document that
  the unchanged version byte cannot prove the reset occurred.
- Observe PWM/servo outputs during induced I2C errors and recovery replay.

## Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| Clean rewrite requires replacing the existing YAML | One-time deployment update | Rewrite the representative configuration directly; add no aliases or shims |
| Input polling still worsens timing | PWM/servo jitter | Explicit intervals, staggering, one scheduled read per loop pass, measurement |
| RGB traffic blocks software outputs | Long pulse or visible flicker | Uniform/coalesced fills, rate limit, hardware stress test |
| Firmware invalid-value side effects | Unexpected output | Validate every range before I2C write |
| Hub disconnect becomes a false alarm | Unsafe automation | Preserve state on read failure and expose parent warning |
| Logical output state is mistaken for physical confirmation | Misleading entity state | Document switch as transported command and light as desired state; retain no false readback claim |
| Recovery probing or firmware stalls flood the bus | Repeated disruption | Rate-limit probes; test interrupted writes and repeating 500 ms stalls |
| Hub resets entirely between transactions | Applied state can be lost without detection | Document absent reset counter; validate known-failure replay and output-only reset behavior |
| Restore mode energizes an output | Physical hazard | Safe-off default and explicit opt-in restoration |
| Public docs leak machine-local context | Privacy/repository hygiene failure | Portable examples and automated privacy scan |

## v2 completion definition

The overhaul is complete only when all of the following are true:

- Protocol helpers cannot encode an invalid endpoint or unsafe RGB range.
- No feature command is sent before application firmware version `2` is
  verified.
- Transport loss invalidates verification and applied-state caches; verified
  recovery restores configuration before replaying desired outputs.
- No legacy PBHUB schema, alias, GPIOPin adapter or compatibility API remains.
- Every entity keeps host-detected transport failure separate from valid state;
  the undetectable firmware ADC-staleness case is documented explicitly.
- Switch state is described as a successfully transported command, light state
  as ESPHome desired state, and neither as physical feedback; the undetectable
  between-transaction hub-reset case is documented explicitly.
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
