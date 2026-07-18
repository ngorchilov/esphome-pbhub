# ESPHome PBHUB v2 Implementation Plan

This is the implementation plan for the `v2` branch. The overhaul is intentionally
split into reviewable phases. The protocol and safety decisions come from
[the firmware datasheet](pbhub-firmware-protocol.md), which remains the source of
truth when implementation details are debated later.

Phase 0 changed documentation only. The later phases implement this plan in
reviewable commits, with their acceptance criteria serving as the completion
boundary for each commit.

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
7. Target the public APIs of ESPHome 2026.7.0 on classic ESP32, with ESP-IDF as
   the primary configuration/code-generation/build target and planned Phase 9
   runtime framework, and Arduino as a compile-only compatibility target.
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
| Endpoint model | Exact channel/index type; valid external numbers are only `0,1,10,11,...,50,51` | The register banks are non-contiguous and the prototype fell back to channel 0 for invalid values |
| Digital input | Native polling `binary_sensor` | A boolean GPIOPin API cannot report I2C failure and generic GPIO polling is excessively frequent |
| Digital output | Native `switch` only | The clean v2 API does not retain the old GPIOPin abstraction |
| ADC | Native raw sensor configured by `slot: 0..5` | ADC exists only on signal A, not on an arbitrary endpoint |
| PWM | `output` mode with fixed frequency near 392 Hz; use digital low/high for encoded duties 0/255 | Firmware version 2 has no frequency control, and its PWM extrema have edge-state defects |
| Servo | Separate `output` mode using the direct pulse register | Application firmware version 2 has a separate, source-derived nominal 50 Hz servo generator; ordinary PWM is not equivalent |
| RGB | One uniform RGB light per configured channel/strip | Every indexed command immediately transmits a strip prefix, making addressable effects inefficient |
| RGB brightness | Scale on the ESP host; keep STM32 brightness at 255 | Firmware brightness is nonlinear and applies one color write late |
| Endpoint conflicts | Reject duplicate ownership | Firmware commands silently replace the current mode of a physical signal |
| Dangerous globals | Excluded | Address mutation wears flash, and reset/IAP is outside the ESPHome component's scope |
| Firmware target | Audited stock protocol reporting application version exactly 2 | The version register is a compatibility guard, not authentication of the exact factory binary |
| Transport API | ESPHome 2026.7.0 `read_register`/`write_register`, returning `i2c::ErrorCode` | These current APIs directly match the PBHUB register transaction model |
| ESPHome target | Exactly 2026.7.0 | No cross-version shims or historical API branches are required |
| Reported output state | Switch publishes after successful transport; light reports ESPHome desired state | ESPHome light state is published before `LightOutput::write_state()`, and firmware has no physical feedback |
| Recovery | Invalidate verification and applied-state caches after transport loss; rate-limit re-probe and replay | Only the I2C address persists across a hub reset |

## Historical prototype gap analysis

The pre-v2 component was a useful prototype. The implementation phases below
closed these gaps; they are retained here as design rationale.

### 1. Endpoint validation accepted impossible pins

Python schemas generally accepted every integer from 0 through 51. Values such
as 2, 9, 22 and 49 did not represent a physical PBHUB signal. C++ register
helpers also mapped an invalid channel back to channel 0. A typo could therefore
control the wrong output instead of failing configuration.

v2 resolution: one shared validator accepts only the twelve exact endpoint
values, and no C++ helper has a channel-0 fallback.

### 2. ADC configuration and C++ interpretation disagreed

The sensor schema accepted a pin-shaped number, while the C++ sensor passed that
number as a channel. A value such as 32 was not channel 3; it became an invalid
channel and fell back to channel 0.

v2 resolution: ADC uses `slot: 0..5`. Signal A is implicit because `B+0x6` is the
only implemented ADC register.

### 3. RGB configuration was not implemented as advertised

The light schema accepted `slot` values through 51 even though the C++ code
expected 0 through 5. `led_count` changed an internal flag but the light never
programmed the count or used a range fill. Each update wrote only LED 0. It wrote
color before brightness, while firmware brightness affected only subsequent
color writes.

v2 resolution: slot and count are validated, firmware brightness is fixed at
255, color is host-scaled and one bounded fill covers all configured LEDs.

### 4. Communication failure was converted into valid state

Digital read returned `false` on I2C error and ADC returned zero. An inverted
input could turn a failed read into `true`, and a real ADC zero could not be
distinguished from disconnection.

v2 resolution: transport methods return success separately from an output value.
Sensors preserve their last value on failure and the parent component reports a
warning state.

This separation covers host-detected transport failures only. A firmware ADC timeout can
leave a stale but valid prior response in the TX buffer while the I2C transaction
succeeds. Application firmware version 2 provides no sequence number or status
bit with which the host could prove sample freshness; the public reference
documents that irreducible limitation.

### 5. Write failures were logged but not reflected in entities

Most write methods ignored the result after logging it. Switch/light/output state
could appear updated even when the command was not transported successfully.
Conversely, an I2C success still does not prove semantic acceptance or the
physical output level because firmware v2 has no such acknowledgement.

v2 resolution: result handling is centralized and desired state is separate
from the last successfully transported command. Switch state publishes only
after transport success. ESPHome's light entity necessarily remains desired
logical state because it publishes before `LightOutput::write_state()`;
transport failures preserve that desired state for replay and surface through
parent communication health. Neither entity is physical feedback.

### 6. Generic GPIO input was the wrong abstraction

ESPHome's generic GPIO binary sensor can call a non-interrupt pin's
`digital_read()` on every main-loop pass. Every call becomes an I2C transaction
and also reasserts input mode in the STM32 firmware. Several inputs can create
continuous bus traffic and worsen PWM/servo timing.

v2 resolution: native PBHUB binary sensors poll at an explicit interval and
publish only successful samples. The custom GPIOPin adapter is removed; digital
outputs use the native PBHUB switch.

### 7. PWM, servo and RTTTL were conflated

The output platform always wrote the PBHUB PWM duty register. The unregistered
C++ servo wrapper was not reachable from YAML. Feeding that PWM output to
ESPHome `servo` did not select the hub's nominal 50 Hz servo mode. Feeding it to
RTTTL could not change the firmware's fixed approximately 392 Hz frequency, so
notes did not follow the melody.

v2 resolution: `mode: pwm` and `mode: servo` are explicit. Dynamic frequency
requests do not fail silently. RTTTL remains unsupported on application firmware
version 2.

### 8. No endpoint ownership model existed

An ADC sensor, GPIO, PWM output, servo and RGB light could all target the same
physical signal. The firmware lets the most recent command silently change its
mode, producing intermittent behavior that looks like a transport problem.

v2 resolution: endpoints are claimed during validation/code generation and
again in C++ as a defensive check. A conflict identifies both owners and fails
loudly.

### 9. Feature guards and class layout were fragile

Output headers were effectively included unconditionally, dummy fallback
classes existed when a feature was disabled, and some method implementations
were outside the same feature guards as their declarations. A hub-only
configuration could compile different code than intended.

v2 resolution: the core transport is independent and entity wrappers compile
only under their matching `USE_*` guards. Warning directives and dummy feature
classes are removed.

### 10. Setup and recovery did not establish or restore device state

Setup logged an address but did not verify that the hub responded or cache
firmware capabilities. Repeated read failures could also flood warnings. A hub
reset lost every configured mode and value except the I2C address, while
host-side caches could still make those values look applied.

v2 resolution: setup requires a successful application-version read of exactly
`2`, reports a useful component state, counts consecutive failures and throttles
repeated logs. Transport loss invalidates firmware verification and every
applied-state cache. After a rate-limited successful re-probe, recovery restores
configured global and RGB state, then replays desired entity outputs before
clearing the warning.

### 11. Documentation disagreed with the actual component

The README used an incorrect default address in examples, named platform values
that did not match the Python module layout, and presented PWM as suitable for
RTTTL and direct ESPHome servo use.

v2 resolution: every public shape is traced to a fixture that passed the target
ESPHome validation/compile matrix, and application-firmware-v2 limits are stated
next to the relevant feature.

## v2 public YAML API

The names and constraints below are the implemented v2 API.

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
the exact factory binary. The setting affects every RGB output on the hub, so
strips that require incompatible timing modes cannot share one PBHUB.

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

The default logical command must be off. Restoring any other state is explicit and occurs
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

servo:
  - id: hub_servo
    output: hub_servo_output
    min_level: 2.5%
    idle_level: 7.5%
    max_level: 12.5%
    transition_length: 0s
```

ESPHome's servo component expresses the pulse as a fraction of its 20 ms frame.
The dedicated PBHUB servo output rounds each nonzero fraction to microseconds and
uses the direct servo-pulse register; it does not branch through the PWM driver.
Only the inclusive range 500 through 2500 microseconds is allowed to reach I2C.
The percentages above therefore map to 500, 1500 and 2500 microseconds.

Servo mode defaults `zero_means_zero` to true. An exact zero uses a digital-low
command to detach because zero is not a valid servo pulse in the STM32 protocol.
Static inversion and power remapping are rejected, as are recognized mutating
YAML actions. A runtime guard prevents a non-neutral transformed pulse from
reaching I2C. Calibration belongs in the ESPHome `servo` component: its stock
defaults of 3%, 7.5% and 12% map to firmware-valid pulses of 600, 1500 and 2400
microseconds. The wider values shown above and reversed calibration such as
12.5%, 7.5% and 2.5% also remain within the firmware-accepted range. These
protocol bounds do not prove the mechanical limits of a connected actuator;
users must calibrate levels for their own servo and linkage.

One PBHUB servo output may have only one ESPHome servo consumer, and
`transition_length` must remain zero because ESPHome generates an output update
on every main-loop pass while transitioning and can cause excessive distinct I2C
pulse writes. RTTTL and generic output power/turn-on actions are rejected for
this output. ESPHome servo state,
including `has_reached_target()`, describes host-side command progression; it
does not confirm successful transport or the servo's physical position. After
detected transport recovery, the output replays its most recent desired pulse or
detach command.

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
update resolves on/off, brightness and RGB on the host, then stages one safe fill
from index 0 across exactly `num_leds` entries. Intermediate states coalesce in a
fair parent-owned queue. The hub sends at most one normal RGB fill per provisional
50 ms interval across all six strips. That host traffic policy is not a claim of
firmware or servo timing safety and remains subject to Phase 9 measurement.

PBHUB lights default to `default_transition_length: 0s`; explicit uniform RGB
effects and transitions remain available and are sampled through the same queue.
Detected transport recovery restores count and firmware brightness before
synchronously replaying the latest desired light state.

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
7. Treat every existing deployment as a downstream consumer, never as a design
   input. Only after the v2 API is frozen, adapt a representative deployment as
   an independent release smoke test without copying its names, identifiers,
   secrets or paths into this repository.
8. Publish the final v2 YAML reference and a concise clean-break notice. No
   migration layer or compatibility promise is provided for pre-v2 configs.

## Python/schema architecture

### Shared validators

Create one shared module-level implementation for:

- `validate_slot`: integer `0..5`.
- `validate_endpoint`: split with `divmod(value, 10)` and require channel `0..5`
  plus index `0 or 1`.
- `validate_led_count`: integer `1..74`.
- required output mode: exact enum `pwm` or `servo`, with distinct generated C++
  output types rather than a runtime branch inside the PWM driver.
- servo output transform: default `zero_means_zero` to true and require
  `inverted: false`, `min_power: 0%`, `max_power: 100%` and
  `zero_means_zero: true`.
- servo consumer contract: allow exactly one ESPHome servo per PBHUB servo
  output; require every `min_level`, `idle_level` and `max_level` to remain within
  2.5% through 12.5%, including reversed calibration; and require
  `transition_length: 0s`.
- servo action contract: reject RTTTL and recursive automation actions that
  target the output through `output.set_min_power`, `output.set_max_power` or
  `output.turn_on`.
- LED timing mode: exact enum/integer 0 or 1.

Error text should include the endpoint formula and accepted values. Platform
schemas must import these validators instead of recreating loose ranges.

### Parent references

Use one canonical `pbhub_id` field for every platform. No aliases are accepted,
so Python validation and C++ code generation share one representation.

The root component schema should extend the standard I2C device schema once;
it should not define `address` twice. `MULTI_CONF` remains supported. Do not
expose ESPHome's generic `setup_priority` escape hatch on the parent: switch
restore state must be loaded before the parent's initial recovery pass.

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
  pbhub_polling.h
  pbhub_recovery.h
  pbhub_rgb_queue.h
  pbhub.h
  pbhub.cpp
  pbhub_entities.h
  pbhub_entities.cpp
```

- `pbhub_protocol.h`: scoped constants, `Endpoint`, register construction and
  endian helpers with no ESPHome entity dependency.
- `pbhub_ownership.h`: host-testable endpoint ownership registry with no
  ESPHome entity dependency.
- `pbhub_polling.h`: fixed-capacity, coalescing FIFO for serialized scheduled
  input and ADC reads, with no ESPHome entity dependency.
- `pbhub_recovery.h`: host-testable health state machine and recovery-client
  orchestration with no ESPHome entity dependency.
- `pbhub_rgb_queue.h`: six-client coalescing FIFO that fairly serializes normal
  RGB fills across one physical hub, with no ESPHome entity dependency.
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
- replay desired output state after all configuration is restored; and
- notify clients after the parent reaches `READY`, so state publication cannot
  trigger automations partway through recovery.

Code generation registers every recovery client before ESPHome calls component
setup. A PBHUB switch uses a priority immediately above the parent's
`setup_priority::IO`: after the I2C bus but before the parent. Its setup resolves
the configured restore mode into desired raw state while the parent is still
`UNVERIFIED`; it performs no I2C transaction. This makes logical-off, or another
explicit restore policy, part of the parent's initial verified recovery pass.
`restore_mode: DISABLED` records no desired state and leaves the switch unknown.

The parent then probes the firmware version, applies configured global timing,
runs the client configuration pass and replays all known desired output states.
It transitions to `READY` only after every replay succeeds. A separate
recovery-complete notification then publishes successfully transported switch
state, preventing switch automations from running partway through recovery. If
the probe, configuration or replay fails, entities retain desired state without
issuing normal feature commands and the bounded recovery sequence retries later.

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
One protocol helper derives the effective command mode from the duty, and the
applied cache records that duty and its derived mode together. This prevents the
cache and register selection from disagreeing while ensuring that transitions
between a digital extremum and intermediate PWM are never incorrectly skipped.

The separate servo output stores either pulse width 500 through 2500 us or zero
as the detach sentinel. Its applied cache changes only after a successful direct
pulse or digital-low transaction. A transport failure preserves the newest
desired pulse or detach state, invalidates the applied cache and relies on the
parent's verified recovery pass to replay it. Invalid levels and runtime
transform violations are local validation failures and do not alter desired
state, generate I2C or enter transport recovery.

Binary-sensor and ADC polling intervals enqueue read requests in a parent-owned,
fixed-capacity FIFO. Duplicate requests from the same entity coalesce while
pending, and the parent performs at most one scheduled read per loop pass. Queued
requests remain pending while the hub is unverified or recovering and resume only
in `READY`. The twelve-entry capacity is sufficient because endpoint ownership
allows at most one polling entity on each of the twelve physical signals.
User-triggered switch, PWM and servo changes remain immediate while the parent is
ready. RGB is intentionally scheduled: each light stores only its latest host-
scaled triple, duplicate queue entries coalesce and a six-client parent FIFO
preserves fairness. The parent permits at most one normal RGB fill every 50 ms
across the whole hub while continuing scheduled input and ADC polls between due
fills. The fixed interval is a provisional traffic cap, not a firmware-derived
safe rate; Phase 9 hardware measurements must validate or revise it. Recovery
replay bypasses this cadence so the parent cannot enter `READY` before the latest
desired state has actually been transported.

### Feature guards

Entity declarations and implementations must use matching `USE_*` guards. There
are no dummy fallback classes and no compile-time warnings for absent optional
features. No GPIOPin compatibility adapter or `dump_summary` signature shim
remains.

## Entity behavior contracts

### Binary sensor

- Default polling interval: 100 ms, configurable.
- A polling deadline queues a request; only the parent performs the I2C read, at
  most one scheduled input/ADC transaction per loop pass.
- Read configures the selected signal as a floating input, as required by the
  firmware.
- The PBHUB wrapper applies inversion only after a successful raw read; ESPHome
  2026.7's binary-sensor base does not provide transport-aware inversion.
- An initial failure leaves state unknown; a later failure preserves the last
  published state.
- No internal pull-up/down options are advertised.

### Switch

- Default restore mode sends a logical-off command.
- Setup resolves the restore policy before the parent performs its initial
  version probe, but records desired state without issuing unverified I2C.
- `restore_mode: DISABLED` leaves the entity unknown until explicitly commanded.
- ESPHome applies inversion before `write_state(bool)`. Store and replay that
  transport-level boolean, then pass the same value to `publish_state()` after a
  successful write so inversion is not applied twice.
- A successfully transported write publishes the requested logical state as
  commanded state, not confirmed physical feedback.
- A failed write leaves state unchanged and marks communication health.
- Recovery replays the most recent desired raw state and publishes it only after
  the parent has transitioned back to `READY`.
- Keep the normal optimistic toggle UI (`assumed_state() == false`); the
  documented entity contract is commanded state even though no physical
  readback exists.

### ADC sensor

- Slot only, signal A implicit.
- Raw range `0..4095`, zero accuracy decimals, an empty unit, measurement state
  class and no invented device class or voltage conversion.
- Default update interval: 1 s, configurable.
- An initial detected I2C failure leaves the sensor unknown; a later failure
  preserves its last published state and does not republish it.
- A response above `4095` is a protocol failure and is not published.
- Aggressive polling increases clock-stretched ADC traffic and can worsen the
  firmware's PWM and servo timing; choose intervals according to actual needs.
- Documentation states that a successful read can still be stale after the
  source-confirmed firmware ADC timeout; v2 cannot guarantee sample freshness.
- Documentation warns that the signal is 3.3 V logic/ADC and must not be driven
  above the documented range.

### PWM output

- Use ESPHome 2026.7.0's normal `FloatOutput` transforms for inversion,
  minimum/maximum power and `zero_means_zero`. Treat the resulting finite level
  as the final electrical duty, clamp it to `0.0..1.0` and round to `0..255`
  without applying any transform twice.
- Before the parent starts recovery, stage logical level zero unless a consumer
  has already supplied a desired level. This creates a deterministic startup
  command while respecting configured `FloatOutput` transforms.
- Reject a NaN level that reaches the driver without changing desired or applied
  state. ESPHome's public `set_level()` clamps positive and negative infinity to
  the normal high and low extrema before the driver sees them.
- Send encoded 0 as digital low, encoded 255 as digital high and only 1 through
  254 through the PWM register.
- Cache the encoded value and its helper-derived effective command mode; skip
  only a repeated, successfully transported value while applied state remains
  known.
- Advertise fixed calculated nominal frequency, not a configurable frequency.
- Override ESPHome 2026.7.0's `update_frequency(float)` hook, leave the fixed
  frequency and caches unchanged and emit one warning per output per boot.
- Reject a `frequency:` option, RTTTL references and use of `mode: pwm` by the
  ESPHome servo component during configuration. Keep the runtime frequency hook
  as defense for direct calls and future consumers.

### Servo output

- Use a dedicated `PbHubServoOutput`, not a mode branch inside
  `PbHubPWMOutput`, and write the firmware's direct pulse register rather than
  angle or PWM duty.
- Convert a finite nonzero frame fraction with
  `round(level * 20,000 us)`, then allow only the inclusive 500 through 2500 us
  range. Reject every other nonzero result locally before any mode-changing I2C
  write.
- Default `zero_means_zero` to true. Exact zero sends digital low to detach and
  caches zero only after successful transport.
- Require neutral static transforms: `inverted: false`, `min_power: 0%`,
  `max_power: 100%` and `zero_means_zero: true`. Recheck those invariants at
  runtime before accepting a nonzero level so direct C++ calls cannot silently
  remap a pulse. Under the supported neutral configuration, exact zero remains a
  detach command.
- Allow exactly one ESPHome servo consumer. Its `min_level`, `idle_level` and
  `max_level` must each fall within 2.5% through 12.5%; standard defaults
  3%/7.5%/12%, calibrated 2.5%/7.5%/12.5% and reversed calibration are valid.
- Require `transition_length: 0s`; a nonzero transition calls the output on every
  main-loop pass and can cause excessive distinct I2C pulse writes. Reject RTTTL
  and runtime `output.set_min_power`, `output.set_max_power` and
  `output.turn_on` actions targeting this output.
- Keep the firmware frame frequency fixed at nominal 50 Hz. A direct runtime
  frequency request changes neither transport nor cache and warns only once.
- Keep desired pulse/detach separate from the last successfully transported
  command. Detected recovery replays the desired command after version 2 is
  reverified.
- Treat ESPHome servo state and `has_reached_target()` as host-side command
  state, not confirmation of I2C success or physical servo position.
- Document firmware jitter and require load testing before safety-sensitive use.

### RGB light

- Validate slot `0..5` and count `1..74`.
- During setup: optionally set the global timing mode once, set count, set
  firmware brightness 255, then send black when the deterministic restore policy
  requires off.
- On update: resolve on/off, brightness, color brightness, RGB and gamma on the
  ESP host; stage one bounded fill at start 0 for exactly `num_leds`.
- Default ordinary changes to no transition. Explicit uniform RGB effects and
  transitions remain supported behind the parent-wide 50 ms scheduler.
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
- Initial-order tests prove no feature transaction occurs before firmware
  verification, configuration restoration precedes output replay and native
  switches load desired restore state before the parent begins recovery.
- Core-only and multi-hub fixtures configure and compile.

### Phase 3 - Native digital entities

Changes:

- Add native polling binary sensor.
- Add native digital switch.
- Add host and YAML validation for the clean native API.

Acceptance:

- An initial input failure remains unknown, and a later failure never changes the
  last published state or flips an inverted sensor to true.
- Polling deadlines enqueue work instead of performing I2C; duplicate requests
  coalesce and the parent executes at most one scheduled input/ADC read per loop
  pass while `READY`.
- Pending reads issue no feature transactions while firmware is unverified or
  recovering and resume after successful recovery.
- Switch setup records the configured restore state without I2C before the
  parent's initial probe; logical off is the default and `DISABLED` remains unknown.
- Switch publishes only successfully transported writes and retains a failed
  request as desired state for recovery replay.
- Detected recovery replays a switch's desired state and publishes it only after
  the parent returns to `READY`.
- Normal and inverted switches transport/replay the correct raw boolean and
  publish the correct logical state without double inversion.
- Duplicate digital ownership fails configuration.
- The former generic-GPIO binary-sensor and switch pin shapes fail validation;
  no GPIOPin adapter or legacy alias is present.

### Phase 4 - ADC and fixed PWM

Changes:

- Finalize the slot-based ADC schema, raw metadata, configuration logging and
  success-only publication contract.
- Rebuild PWM as a setup-ordered component with standard ESPHome output
  transforms, exact duty conversion, helper-derived mode-aware caching and
  fixed-frequency reporting.
- Reject unsupported variable-frequency consumers and add dedicated host and
  YAML contract tests.

Acceptance:

- All six ADC slots produce the correct register and read exactly two
  little-endian bytes.
- Valid ADC zero and 4095 publish exactly; an initial I2C failure remains unknown
  and a later failure preserves the last state without republishing it.
- An ADC response above 4095 enters bounded protocol recovery without publishing,
  while an invalid runtime slot produces no I2C transaction or health change.
- ADC signal A and PWM signal B in one slot can coexist; a successful ADC poll
  does not invalidate or replay the PWM cache.
- The successful-but-stale response possible after the firmware's internal ADC
  timeout remains documented as an irreducible limitation.
- PWM encoded 0 sends digital low, encoded 255 sends digital high and values 1
  through 254 use the correct PWM register and duty byte.
- Duty rounding, inversion, power scaling and both `zero_means_zero` behaviors
  match ESPHome 2026.7.0 `FloatOutput` semantics without double transforms.
- Transitions between a digital extremum and intermediate PWM are not skipped by
  the applied-state cache; repeated values and levels in one encoded-duty bucket
  do not generate duplicate I2C writes after success.
- Startup stages a deterministic logical-zero command before parent recovery,
  unless a desired level was already supplied.
- A failed write keeps the newest desired level, invalidates applied state and
  replays the exact desired command only after firmware version 2 is reverified.
- Runtime frequency requests leave transport and caches unchanged and warn only
  once; `frequency:`, RTTTL use and pseudo-servo through `mode: pwm` fail YAML
  validation.
- A NaN driver-level value cannot become an output command, while infinities
  passed through ESPHome's public API clamp to the normal extrema.
- Detected recovery replays the desired encoded PWM level using the same
  digital-extrema rule.
- RTTTL is no longer shown as supported.

### Phase 5 - Servo mode

Changes:

- Add a distinct direct-pulse output type and exact `mode: servo` schema/codegen.
- Convert ESPHome servo frame fractions through the 20 ms frame with local
  500-through-2500 us validation.
- Implement digital-low detach, success-only applied caching and recovery replay.
- Validate static transforms, servo calibration/consumer constraints and
  unsupported runtime consumers/actions.

Acceptance:

- Minimum, center and maximum standard pulses encode as 500, 1500 and 2500 us.
- ESPHome's defaults encode as firmware-valid 600, 1500 and 2400 us; the full
  firmware-accepted range and reversed calibration pass schema validation
  without claiming the connected actuator's mechanical limits.
- Zero defaults to a true zero and detaches with digital low without sending an
  invalid servo pulse.
- A NaN that reaches the driver and every out-of-range nonzero pulse result fail
  locally, preserve desired/applied state and do not change hub mode or health;
  public `FloatOutput` calls retain ESPHome's normal infinity clamping behavior.
- Inverted or power-remapped configurations fail validation. A runtime guard
  prevents a non-neutral transformed pulse from reaching I2C; under the
  supported neutral configuration, exact zero remains a detach command.
- Nonzero transitions, multiple servo consumers, RTTTL and runtime power/turn-on
  actions targeting the PBHUB servo output fail configuration.
- Runtime frequency requests leave the fixed 50 Hz transport and caches
  unchanged and warn only once per output.
- Repeated successfully transported pulse or detach commands are deduplicated;
  a failed write preserves desired state and detected recovery replays it only
  after firmware version 2 is reverified.
- Documentation makes no transport or physical-position claim from ESPHome
  servo state or `has_reached_target()`.

### Phase 6 - RGB rebuild

Changes:

- Correct slot/count validation and initialization.
- Apply host brightness and uniform fill.
- Add optional firmware-v2 LED timing mode at parent scope.
- Default ordinary changes to zero transition and coalesce normal fills through
  a fair parent-wide queue with a provisional 50 ms aggregate interval.

Acceptance:

- Counts 1 and 74 pass; 0 and 75 fail.
- Every generated fill satisfies `start + count <= configured_count <= 74`.
- An invalid range fails locally and produces no I2C transaction.
- Brightness never uses the STM32's defective scaler below 255.
- Host-scaled RGB byte boundaries 0, 127, 128, 254 and 255 are encoded exactly.
- The light turns fully off with an explicit black fill.
- Multiple strips cannot burst normal fills or starve one another; only the
  newest queued state of each strip is transported.
- Detected recovery restores timing/count/brightness before replaying the
  desired fill; a failed step does not advance to output replay.

### Phase 7 - Framework and bus-topology validation

Changes:

- Make classic ESP32 with ESP-IDF the canonical configuration, code-generation
  and detailed compile target, and the planned Phase 9 runtime/hardware
  framework. Keep strict host C++ logic tests framework-neutral.
- Keep the detailed positive, negative, generated-contract and firmware-compile
  suite on ESP-IDF so framework-dependent code paths are proved once instead of
  multiplying every logical fixture across frameworks. Keep the strict host C++
  tests framework-neutral.
- Add two shared framework smoke scenarios: one core-only configuration and one
  full-surface configuration exercising every PBHUB entity domain, feature guard
  and supported bus topology. Compile both scenarios under ESP-IDF and Arduino.
- In the shared full-surface scenario, cover a direct bus, two physical I2C
  buses, multiple hubs sharing one bus at distinct addresses and TCA9548A
  virtual channels. Include hubs using the same default address on separate
  multiplexer channels. Address `0x62` represents a hub pre-addressed outside
  this component; v2 does not mutate device addresses.
- Update the local runner to keep detailed logic/configuration validation
  separate from the small paired-framework compile smoke matrix.
- Remove ESP8266 and ESP32-S3 from the v2 validation and support boundary.

Acceptance:

- The framework-neutral host C++ tests pass, and the detailed positive/negative
  schema, generated-contract and firmware-compile suite passes on classic ESP32
  with ESP-IDF under ESPHome 2026.7.0.
- The core-only and full-surface fixtures compile on classic ESP32 under both
  ESP-IDF and Arduino.
- The full-surface fixture activates `USE_BINARY_SENSOR`, `USE_OUTPUT`,
  `USE_OUTPUT_FLOAT_POWER_SCALING`, `USE_SENSOR`, `USE_SWITCH` and `USE_LIGHT`,
  and exercises both PWM and servo output code generation.
- Direct I2C, two physical I2C buses, same-bus hubs at distinct addresses and
  TCA9548A virtual-channel configurations validate and compile under both
  frameworks through the shared full-surface scenario.
- No ESP8266 or ESP32-S3 fixture or v2 support claim remains.
- Arduino compile success is described only as compile compatibility, not as
  runtime or real-hardware support.

### Phase 8 - Documentation and v2 public reference

Changes:

- Rewrite README installation and all entity examples.
- Publish a concise clean-break notice and the complete new YAML reference.
- Document the self-reported firmware-version guard, commanded-state semantics,
  undetectable between-transaction reset, PWM, servo, RGB and voltage limits.
- Document that same-bus multi-hub examples require externally pre-addressed
  devices because v2 deliberately exposes no runtime address mutation.
- Publish only the framework, target and topology support demonstrated by Phase
  7 under ESPHome 2026.7.0.
- Trace each public example to the already validated Phase 7 fixture from which
  it is drawn. Because this phase changes documentation only, reuse the Phase 7
  compile evidence instead of rerunning the complete matrix without a software
  or fixture change.

Acceptance:

- Every public example names the validated ESP-IDF fixture from which it is
  drawn; topology examples also name the paired fixture compiled under both
  frameworks.
- No README platform names or addresses disagree with the schemas.
- Every claimed example path is covered by the existing Phase 7 ESPHome 2026.7.0
  matrix, without implying Arduino runtime validation or new hardware evidence.
- Privacy scan is clean.

### Phase 9 - Hardware validation and release

Changes:

- Execute the hardware matrix below on classic ESP32 with ESP-IDF.
- Record measured values separately from calculated firmware values.
- Fix host-side issues exposed by measurement.
- Tag a v2 prerelease, validate a representative deployment, then prepare
  merge/release.
- Add an Arduino runtime-support claim only after a separate Arduino hardware
  validation pass and an explicit plan update.

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

- Classic ESP32 with ESP-IDF is the canonical configuration, code-generation
  and detailed compile target, and the planned Phase 9 real-hardware target.
- Classic ESP32 with Arduino is a secondary compile-only compatibility target.
- ESP8266 and ESP32-S3 are outside the v2 validation and support boundary.
- Detailed positive/negative schema, generated-contract and firmware-fixture
  coverage runs once against ESP-IDF; strict host C++ tests remain
  framework-neutral.
- A core-only fixture and one full-surface fixture compile under both ESP-IDF
  and Arduino. The full-surface fixture exercises all PBHUB entity domains,
  feature guards and supported topologies: direct I2C, two physical buses,
  multiple hubs sharing one bus and TCA9548A virtual channels, including the
  same default PBHUB address on isolated channels.
- Planned Phase 9 real-hardware validation uses ESP-IDF only unless Arduino
  runtime support is separately approved after its own hardware pass.

### Positive fixtures

- Hub only.
- Native digital input and switch across all twelve endpoints, including
  inversion, polling intervals and restore modes.
- All twelve valid endpoints.
- ADC on all six slots.
- PWM mode with inversion, power scaling, both zero behaviors and independent
  ADC signal A/PWM signal B ownership in one slot.
- Servo mode through one ESPHome servo using stock, calibrated and reversed
  firmware-valid levels with zero transition length.
- RGB counts 1 and 74.
- A core-only configuration with no optional PBHUB entity domains.
- A full-surface configuration activating every PBHUB entity domain and feature
  guard.
- Direct I2C and two physical I2C buses.
- Multiple hubs sharing one bus at distinct addresses.
- TCA9548A virtual channels, including hubs at the same default address on
  isolated channels.

### Negative fixtures

- Invalid endpoints including 2, 9, 19 and 49.
- Slots below 0 or above 5.
- RGB counts 0 and 75.
- Unknown output and LED timing modes.
- ADC configured with an endpoint instead of a slot.
- Invalid native binary-sensor and switch endpoints.
- Configurable PBHUB PWM frequency, RTTTL references and ESPHome servo using a
  PBHUB output in `mode: pwm`.
- Servo inversion, non-neutral power scaling, `zero_means_zero: false`,
  out-of-range calibration levels, nonzero transitions and multiple Servo
  consumers.
- RTTTL and runtime power/turn-on actions targeting a PBHUB servo output.
- Duplicate binary sensors, duplicate switches and cross-domain features
  claiming the same endpoint.

### Checks

The local validation runner should perform, as applicable:

1. Host-side protocol, ownership, scheduling, recovery and entity behavior
   tests.
2. `esphome config` for detailed positive fixtures under classic ESP32 with
   ESP-IDF.
3. Expected-failure assertions for negative fixtures under ESP-IDF.
4. Generated-source contract checks under ESP-IDF.
5. Detailed positive-fixture compilation under ESP-IDF.
6. Paired compilation of only the core-only and full-surface smoke scenarios
   under ESP-IDF and Arduino; the full-surface pair contains the direct-bus,
   two-physical-bus, same-bus multi-hub and TCA9548A topology matrix.
7. C++ formatting/static checks available without tracked dotfiles.
8. `git diff --check`.
9. Privacy scan for absolute home paths, deployment identifiers, hidden workflow
   references and secret-like values.

Because hidden files are deliberately excluded, do not add `.github`, formatter
dotfiles or local workflow notes. A hosted CI service can be revisited only if the
repository policy changes explicitly.

## Required real-hardware matrix

Execute this matrix on classic ESP32 with ESP-IDF. Arduino compilation is a
separate compatibility signal and does not become a runtime or hardware-support
claim unless an Arduino hardware pass is later approved and recorded.

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
- Verify ESPHome's default 600, 1500 and 2400 us calibration and one reversed
  calibration without changing pulse bounds.
- Verify detach drives low and stops pulses.
- Test first-frame behavior after mode changes.
- Force a detected transport failure, then verify that recovery replays the most
  recent pulse or detach command only after version 2 is reverified.
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
- Test a direct bus and both physical ESP32 I2C controllers as separate buses.
- Test multiple hubs sharing one physical bus at distinct addresses.
- Test TCA9548A virtual-channel routing, including two hubs using the same
  default address on isolated channels.
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
| Logical output state is mistaken for physical confirmation | Misleading entity state | Document switch as transported command, light as desired state and servo state/`has_reached_target()` as host command progression; retain no false readback claim |
| Recovery probing or firmware stalls flood the bus | Repeated disruption | Rate-limit probes; test interrupted writes and repeating 500 ms stalls |
| Hub resets entirely between transactions | Applied state can be lost without detection | Document absent reset counter; validate known-failure replay and output-only reset behavior |
| Restore mode energizes an output | Physical hazard | Logical-off command default and explicit opt-in restoration |
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
  as ESPHome desired state and servo state/`has_reached_target()` as host-side
  command progression. None is described as physical feedback; the undetectable
  between-transaction hub-reset case is documented explicitly.
- Endpoint conflicts fail before normal operation.
- Digital, ADC, PWM, servo and uniform RGB behavior matches the datasheet.
- The detailed configuration, code-generation and compile suite passes under
  ESPHome 2026.7.0 on classic ESP32 with ESP-IDF.
- The core-only and full-surface smoke fixtures compile under both ESP-IDF and
  Arduino, without presenting Arduino compilation as runtime validation.
- Direct-bus, two-physical-bus, same-bus multi-hub and TCA9548A topology paths
  validate and compile under both frameworks through the shared full-surface
  fixture; no ESP8266 or ESP32-S3 support is claimed.
- Public examples validate and compile under each framework explicitly claimed
  for that example.
- Required hardware tests are recorded, with measured and calculated behavior
  clearly separated, on classic ESP32 with ESP-IDF.
- A representative deployment using only the clean v2 API runs through a
  meaningful soak period.
- README and the clean-break release note state stock-firmware limitations
  plainly.
- Repository privacy and dotfile policy remain intact.
- No reset, firmware-flashing or runtime address-mutation code is present.
