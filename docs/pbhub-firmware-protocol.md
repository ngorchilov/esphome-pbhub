# PBHUB v1.1 Internal Firmware Protocol Datasheet

This document is the protocol source of truth for the ESPHome PBHUB component.
It describes the behavior implemented by M5Stack's STM32 firmware, including
behavior that is missing from the published register sheet and behavior that
appears to be defective.

Firmware redesign, flashing and recovery engineering are maintained separately
in the firmware project and are outside this repository's scope.

## Scope and evidence

Target hardware: M5Stack Unit PbHub v1.1, SKU U041-B, based on the
STM32F030F4P6.

Research baseline:

- Internal firmware: commit
  [`6de9c0a9f2a3bffdbf17313d3a5aa933228ee772`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/tree/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772)
  from 2025-03-27.
- Factory image:
  [`U041-B_Unit-Pbhub-v1.1_STM32_V2_JIE_20250312_0x0.hex`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/blob/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772/firmware/U041-B_Unit-Pbhub-v1.1_STM32_V2_JIE_20250312_0x0.hex).
- Published protocol: M5Stack's
  [V2 I2C protocol sheet](https://github.com/m5stack/M5Unit-PbHub/blob/1974058947d5556e62957681082e0c082da14071/docs/V2/PbHub_V2_I2C_Protocol.pdf),
  pinned to the same revision as the host reference.
- Host reference: M5Stack's
  [Arduino library](https://github.com/m5stack/M5Unit-PbHub/tree/1974058947d5556e62957681082e0c082da14071).
- Hardware reference: M5Stack's
  [product page and schematic](https://docs.m5stack.com/en/unit/pbhub_1.1).

Evidence terms used below:

- **Source-confirmed** means the behavior follows directly from the upstream C
  source at the pinned commit.
- **Published** means M5Stack documents the behavior, but it may not expose all
  implementation details.
- **Calculated** means a value is derived from source constants. It still needs
  measurement on real hardware.
- **Hardware-unverified** means this project has not yet measured the behavior.

When the published sheet and source differ, this document records both and uses
the source behavior for component implementation. Hardware measurements should
be added later without erasing the distinction between measured and derived
facts.

The upstream repository does not provide a reproducible build-and-merge path
that proves its application source is byte-identical to the supplied factory
image. Application behaviors below are therefore source-confirmed, not claims of
binary identity with every shipped unit.

### Upstream source map

- Register decoding, mode changes, ADC filtering, PWM/servo loop and global
  commands:
  [`code/Core/Src/main.c`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/blob/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772/code/Core/Src/main.c).
- Application I2C buffering and interrupt state machine:
  [`code/Core/Src/i2c_ex.c`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/blob/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772/code/Core/Src/i2c_ex.c).
- 1 microsecond timer configuration:
  [`code/Core/Src/tim.c`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/blob/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772/code/Core/Src/tim.c).
- RGB buffers, brightness and waveform generation:
  [`code/Core/Src/ws2812.c`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/blob/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772/code/Core/Src/ws2812.c)
  and
  [`code/Core/Inc/ws2812.h`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/blob/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772/code/Core/Inc/ws2812.h).
- Persistent I2C address storage:
  [`code/Core/Src/flash.c`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/blob/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772/code/Core/Src/flash.c).
- I2C IAP bootloader:
  [`code/bootloader/IAPTest_LL/IAPTest_LL/Core/Src/main.c`](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/blob/6de9c0a9f2a3bffdbf17313d3a5aa933228ee772/code/bootloader/IAPTest_LL/IAPTest_LL/Core/Src/main.c).

## Device overview

| Property | Value | Evidence |
|---|---|---|
| MCU | STM32F030F4P6 | Published and source-confirmed |
| Application clock | 48 MHz | Source-confirmed |
| I2C address | `0x61` by default | Published and source-confirmed |
| Channels | 6 | Published and source-confirmed |
| Signals per channel | A/index 0 and B/index 1 | Source-confirmed |
| Signal voltage | 3.3 V | Published protocol note and schematic |
| Port supply | 5 V | Schematic |
| ADC | Signal A only, 12-bit result | Source-confirmed |
| Digital/PWM/servo | Both A and B | Source-confirmed |
| RGB data | Signal B only, 24-bit GRB on wire | Source-confirmed |
| Maximum RGB count | 74 LEDs per channel | Published and source-confirmed |

The firmware maps the six external channels to the MCU as follows:

| Channel | Signal A / index 0 | Signal B / index 1 |
|---:|---|---|
| 0 | PA0 / ADC_IN0 | PA6 |
| 1 | PA1 / ADC_IN1 | PA7 |
| 2 | PA2 / ADC_IN2 | PB1 |
| 3 | PA3 / ADC_IN3 | PF0 |
| 4 | PA4 / ADC_IN4 | PF1 |
| 5 | PA5 / ADC_IN5 | PA13 / SWDIO |

I2C uses PA9 for SCL and PA10 for SDA. The firmware configures both as
open-drain, enables clock stretching and the analog filter, and does not enable
MCU-internal pull-ups. The board schematic shows external 10 kΩ pull-ups from
both lines to 3.3 V.

### Startup state

Only the I2C address persists across reset. Other state returns to these values:

- PWM and servo modes disabled, with cached values zero.
- Per-channel LED count set to 74.
- Per-channel LED brightness set to 255.
- LED timing mode set to 0.
- RGB color buffers cleared by static initialization.
- Cached digital output values zero.

GPIO initialization is not uniform. A0 starts in analog mode, A1 through A5
start as floating inputs, and B0 through B4 start as push-pull outputs driven
low. B5 is PA13, which is also SWDIO, and is not configured by the generated
startup GPIO function. A host must not rely on a signal's power-on mode; it must
issue the command that establishes the required mode.

Application clock startup contains unbounded waits for flash and oscillator
state. A clock-start failure can therefore leave the application unavailable
before I2C starts; from the host this is indistinguishable from another
transport failure.

## I2C transaction model

The application uses a 7-bit slave address. A command is a register byte
followed by an optional payload.

Write:

```text
START  address+W  register  payload...  STOP
```

Read with a repeated start:

```text
START  address+W  register  RESTART  address+R  response...  STOP
```

A STOP-separated pointer write followed by a read transaction also works. The
firmware processes received bytes on STOP or when the next address match occurs.
For a repeated-start read, it prepares the response before releasing the I2C
address phase, so an ADC request may clock-stretch while all samples are taken.

Application RX and TX buffers are 50 bytes. Every supported application command
is shorter than that limit.

### Transport caveats

The protocol has no semantic status or error response. An I2C ACK means bytes
were transported, not that the register, length or value was accepted.

- Responses are cyclic. If the host over-reads a response, the firmware repeats
  it from byte zero.
- A short read leaves the TX cursor partway through the response. A later bare
  read resumes there unless the host first selects a register again.
- Unsupported reads and ADC timeouts do not replace the TX buffer. They can
  return bytes from an earlier successful command. Before the first successful
  response is prepared, static initialization makes the buffer return zero.
- RX overflow wraps to the beginning of the 50-byte buffer instead of rejecting
  the transaction.
- Command callbacks run inside the I2C interrupt handler. ADC conversion, RGB
  output, flash erase/program and address reinitialization therefore delay the
  firmware's main-loop work.
- I2C error interrupts are enabled, but the error callback does not clear,
  report or recover from errors. Persistent or retriggered bus-fault behavior is
  a source-derived risk and remains hardware-unverified.
- Incomplete-write recovery can reinitialize I2C and block the main loop for
  500 ms. Its trigger state is not cleared by that recovery path, so the delay
  can repeat until a later STOP clears the state.

Host requirements follow directly: select a register for every read, request
exactly the documented response length, validate every value before sending it,
and never probe unknown registers expecting a reliable error.

## Channel and endpoint encoding

The low nibble is an operation. The high nibble selects a channel using a
non-contiguous base table:

| Channel | Register base |
|---:|---:|
| 0 | `0x40` |
| 1 | `0x50` |
| 2 | `0x60` |
| 3 | `0x70` |
| 4 | `0x80` |
| 5 | `0xA0` |

There is no channel at base `0x90`.

The ESPHome component represents an endpoint as:

```text
endpoint = channel * 10 + index
```

The only valid endpoint values are:

```text
0, 1, 10, 11, 20, 21, 30, 31, 40, 41, 50, 51
```

The firmware decoder has an undocumented alias: `0x00` through `0x0F` operate
on channel 0 because a zero high nibble passes the later channel-range check.
This is treated as a firmware defect. Hosts must use the published `0x40` bank.

## Per-channel register map

Let `B` be the channel base from the preceding table. Payload and response
lengths below exclude the register byte. Multi-byte numeric values are
little-endian.

| Register | Access | Write payload | Read response | Function |
|---|---|---:|---:|---|
| `B+0x0` | R/W | 1 byte | 1 byte | Digital output/cache, signal A |
| `B+0x1` | R/W | 1 byte | 1 byte | Digital output/cache, signal B |
| `B+0x2` | R/W | 1 byte | 1 byte | PWM duty, signal A |
| `B+0x3` | R/W | 1 byte | 1 byte | PWM duty, signal B |
| `B+0x4` | R | none | 1 byte | Digital input sample, signal A |
| `B+0x5` | R | none | 1 byte | Digital input sample, signal B |
| `B+0x6` | R | none | 2 bytes | 12-bit ADC sample, signal A |
| `B+0x7` | - | - | - | Reserved and unimplemented |
| `B+0x8` | R/W | 2 bytes | 2 bytes | RGB LED count |
| `B+0x9` | R/W | 5 bytes | 5 bytes | Set one RGB LED |
| `B+0xA` | R/W | 7 bytes | 7 bytes | Fill an RGB LED range |
| `B+0xB` | R/W | 1 byte | 1 byte | RGB brightness scaler |
| `B+0xC` | R/W | 1 byte | 1 byte | Servo angle, signal A |
| `B+0xD` | R/W | 1 byte | 1 byte | Servo angle, signal B |
| `B+0xE` | R/W | 2 bytes | 2 bytes | Servo pulse in microseconds, signal A |
| `B+0xF` | R/W | 2 bytes | 2 bytes | Servo pulse in microseconds, signal B |

Write length checks are inconsistent. Digital, PWM, angle and brightness use
the first payload byte and ignore trailing bytes. LED count, single LED, fill
and direct servo pulse require their exact documented lengths. Hosts must always
send the exact length in the table. Among global writes, `0xFA` and `0xFF`
require exactly one payload byte, while `0xFD` uses the first payload byte and
ignores trailing data.

### Digital output: `B+0x0`, `B+0x1`

The first payload byte is cached in full, but only bit 0 controls the physical
level. Reading the same register returns the full cached byte rather than the
actual pin level.

A write:

- disables PWM and servo on the selected signal;
- clears the active PWM and servo pulse values;
- configures a push-pull output with no pull;
- drives the physical level from payload bit 0.

The host should send only `0x00` or `0x01`.

### Digital input: `B+0x4`, `B+0x5`

Selecting and reading one of these registers is not side-effect free. It:

- disables PWM and servo on the selected signal;
- clears the active PWM and servo pulse values;
- reconfigures the signal as an input with no pull;
- samples and returns one byte, normally 0 or 1.

The firmware offers no pull-up, pull-down, interrupt, debounce or edge-capture
configuration. Short pulses can be missed between host polls.

### ADC: `B+0x6`

ADC is available only on signal A. Each read:

1. disables PWM and servo on signal A;
2. reconfigures signal A as analog;
3. selects ADC channel equal to the PBHUB channel number;
4. takes 22 12-bit samples;
5. discards one maximum and one minimum;
6. returns the integer mean of the remaining 20 samples.

The two-byte response is little-endian and nominally ranges from 0 to 4095. The
published protocol sheet states a 3.3 V input range. Register `B+0x7` does not
provide an ADC input for signal B; it is unimplemented.

If ADC conversion times out, the firmware installs no new response. The master
can receive a stale prior ADC response. A stale value remains a valid `0..4095`
value and the I2C transaction itself can succeed, so range checks and transport
health cannot reliably detect this firmware-level timeout. Hosts can detect bus
errors, but ADC freshness is an irreducible limitation of the stock firmware.

The conversion wait counter is shared across all 22 samples in one request and
is not reset after each sample. The 32,000-iteration limit is therefore a
cumulative busy-wait budget, not a per-sample timeout. ADC calibration and ready
waits during startup also break out after their own limits while allowing the
application to continue without recording that ADC initialization failed.

## PWM

Registers `B+0x2` and `B+0x3` select PWM on signal A or B. The one-byte duty
value ranges from 0 to 255. Reading the register returns the cached duty even if
another command has since changed the signal to a different mode.

This is software-polled GPIO PWM, not timer output compare:

- TIM16 runs with a nominal 1 microsecond tick.
- The main loop resets the counter at `>= 2550`.
- Duty byte `d` becomes a high threshold of `d * 10` microseconds.
- The calculated nominal frequency is `1,000,000 / 2550`, approximately
  **392.16 Hz**.
- The calculated nominal duty fraction is approximately `d / 255`; the source
  uses an inclusive `counter <= d * 10` comparison. Under ideal loop timing, a
  non-saturated high interval is approximately `(10 * d + 1) / 2550` of a
  period.
- All twelve signals share one timer phase.

There is no frequency register or frequency variable in application firmware
version 2. This confirms the limitation reported in
[upstream issue #1](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/issues/1).
Passive-buzzer notes cannot be varied and loads requiring another PWM frequency
cannot be driven correctly by this firmware. The issue was still open when this
datasheet was researched on 2026-07-18; a
[second user reported](https://github.com/m5stack/M5Unit-PbHub-Internal-FW/issues/1#issuecomment-3580005060)
the same limitation for a fan that requires approximately 2 kHz PWM.

The calculated timing is not a guarantee. Counter reset and GPIO edges happen
only when the main loop observes the timer. Interrupt work and long callbacks can
lengthen periods, delay falling edges or stretch a high pulse.

Source-confirmed edge cases:

- Duty 255 remains continuously high because the counter resets before it can
  exceed the 2550 threshold.
- Duty 0 is explicitly driven low when configured, but the `counter <= 0` test
  can theoretically create a very narrow high glitch.
- Internal PWM edge state is not reset on mode changes. One sequence can leave a
  requested duty-255 output low: PWM was high, another mode drives the pin low,
  then PWM 255 is enabled while the stale internal state still says "high".

The host component can avoid both steady-state edge cases by using digital low
for encoded duty 0, digital high for encoded duty 255 and the PWM registers only
for duties 1 through 254. Transitions between those modes and intermediate PWM
still require oscilloscope validation because firmware edge-state flags survive
mode changes.

## Servo

Servo angle registers accept `0..180`. The source maps angle to pulse width as:

```text
pulse_us = 500 + floor(angle * 2000 / 180)
```

Direct pulse registers accept `500..2500` microseconds as a little-endian
16-bit value.

Servo output is also software-polled:

- TIM17 uses a nominal 1 microsecond tick.
- The main loop resets it at `>= 20000`.
- The calculated frame rate is 50 Hz.
- All twelve signals share one frame phase.

An angle write updates both the cached angle and pulse. A direct pulse write
updates only the pulse, so the angle readback can be stale. There is no explicit
detach command; a digital-low command is the safe host mechanism for disabling
servo pulses.

Invalid values are not clean no-ops. The firmware disables PWM, enables servo
and configures the output before checking the angle or pulse range. A rejected
value can therefore reactivate an old servo pulse or leave a zero-pulse servo
mode enabled. The host must validate ranges before writing.

Servo edge state is not reset on mode changes, and all edges depend on the main
loop. ADC, RGB, flash and I2C recovery work can distort the first frame or
stretch a high pulse. Hardware testing under worst-case bus traffic is required.

## RGB output

Single-LED and fill writes claim signal B. They disable PWM and servo on B and
configure it as a push-pull output before validating the requested index or
range. Consequently, even a color write that renders nothing can take the pin
away from its prior mode. LED-count and brightness writes only update cached
configuration; they do not change the pin mode or transmit data.

The firmware allocates an independent 74-entry color buffer for each channel.
Payload colors are expressed as R, G, B; the serialized 24-bit wire order is
G, R, B. There is no fourth white channel, so RGBW SK6812 devices are not fully
supported.

Every in-range single-LED or fill write transmits immediately. There is no
separate buffered `show` command:

- A single-LED command updates one cached entry and retransmits the prefix from
  LED 0 through that index.
- A fill command updates a range and retransmits the prefix through the last
  affected index, or the configured strip length in its clipping branch.

This makes the protocol suitable for a uniform RGB light or occasional indexed
updates. It is inefficient for ESPHome addressable effects because every small
change initiates a new bit-banged transmission inside the I2C callback.

### LED count: `B+0x8`

Payload: `count_le16`.

The count defaults to 74. Values above 74 are clamped to 74; zero is accepted.
Zero makes indexed updates fail their `index < configured_count` check, and the
firmware defines no other zero-count strip behavior. Changing the count neither
clears the color buffer nor transmits a frame. The ESPHome component must use
`1..74` for a configured light.

### One LED: `B+0x9`

Payload:

```text
index_lo, index_hi, red, green, blue
```

The update executes only when `index < configured_count`. Reading the register
returns the last five payload bytes, not the current rendered color after later
operations. Before the first write, static initialization makes this readback
all zero.

### Fill range: `B+0xA`

Payload:

```text
start_lo, start_hi, count_lo, count_hi, red, green, blue
```

The bounds check is defective. It compares `count < configured_count` instead of
checking `start + count <= configured_count`. A request such as start 70, count
10 with a 74-LED configuration writes beyond the fixed color buffer and can
corrupt RAM.

Host safety rule:

```text
1 <= count <= configured_count
start < configured_count
start + count <= configured_count
configured_count <= 74
```

The ESPHome component must enforce these bounds and should use a single safe
fill over the configured range for a uniform light.

Reading the fill register returns the last seven requested payload bytes,
including an invalid or clipped request, rather than the effective range or
rendered colors.

### Brightness: `B+0xB`

Brightness defaults to 255. Changing it does not rescale or retransmit existing
colors; it affects only later writes into the color buffer.

The scaling formula is also defective. For values 1 through 254 it effectively
computes:

```text
output = input / floor(255 / brightness)
```

Values 128 through 254 therefore produce no attenuation, and the remaining steps
are highly nonlinear. The v2 host component should keep firmware brightness at
255 and apply on/off and brightness scaling to RGB values before sending them.

### LED timing mode: global `0xFA`

The one-byte value is global to the whole hub, not per channel. It defaults to 0
and is not persistent. Values other than 0 or 1 are ignored.

M5Stack's V2 protocol sheet associates the modes with these device families:

| Mode | Published device families |
|---:|---|
| 0 | WS2812, WS2815, WS2816, SK6812 |
| 1 | SK6822, APA106, PL9823 |

The source implements two different hand-timed GPIO/NOP routines but does not
name their intended devices. Exact waveform timing is compiler/build dependent
and remains hardware-unverified.

## Mode ownership and side effects

Each physical signal has one effective mode. Commands can silently take the
signal away from another configured feature.

| Command | Signal claimed | Modes disabled or replaced |
|---|---|---|
| Digital write A/B | Selected signal | PWM and servo |
| Digital read A/B | Selected signal | PWM and servo |
| ADC read | Signal A | Digital mode, PWM and servo |
| PWM A/B | Selected signal | Digital mode and servo |
| Servo A/B | Selected signal | Digital mode and PWM |
| RGB single/fill write | Signal B | Digital mode, PWM and servo |

LED-count and brightness writes, all RGB-related reads and global LED timing
mode changes do not claim a signal. The ESPHome RGB entity still owns signal B
exclusively because each rendered state uses a fill write.

There is no active-mode register. Most readbacks are cached command values, not
the current pin configuration. A host should prevent multiple ESPHome entities
from owning the same endpoint rather than allowing runtime mode fights.

## Global registers

| Register | Access | Payload/response | Meaning | Publication status |
|---:|---|---|---|---|
| `0xFA` | R/W | 1 byte | Global LED timing mode, 0 or 1 | Published |
| `0xFC` | R | 1 byte | Bootloader version byte at `0x08000FFF` | Undocumented |
| `0xFD` | W | first payload byte `1` | Deinitialize peripherals and reset | Undocumented |
| `0xFE` | R | 1 byte | Application firmware version, hard-coded `2` | Published |
| `0xFF` | R/W | 1 byte | Active and persistent I2C address | Published |

Register `0xFE` is a self-reported protocol-version guard. A value of 2 does not
authenticate the exact factory image or distinguish modified firmware that keeps
the same version byte.

### Address register: `0xFF`

The write accepts any value from 1 through 127, including I2C-reserved addresses.
It stores the byte in flash and immediately reinitializes I2C at the new address.
The host receives no confirmation that persistence succeeded. The flash helper
can erase and retry the page up to 21 times, but the register handler ignores its
final result. A failed write can therefore leave the new address active only in
RAM until reset without having persisted it.

Persistence uses the last 1 KiB flash page at `0x08003C00`. Its record contains
a `0xAA55` header, a 16-bit length and the address byte. There is no checksum or
boot-time range validation. Each address write erases and rewrites the full page,
even when the address is unchanged.

The component should not expose address mutation as a normal runtime control.
Users can configure the already-programmed address in YAML.

### Reset register: `0xFD`

Writing a first payload byte of 1 deinitializes I2C, ADC and both timers, then
requests an MCU reset. It does not set a persistent bootloader flag. After reset,
the bootloader merely presents its normal short IAP window before returning to a
valid application.

The component does not expose this register.

## Excluded mutation and recovery controls

The source contains a reset register, a legacy I2C bootloader and persistent
address mutation. These paths lack the acknowledgement, verification and
hardware-proven recovery contract required for a safe ESPHome feature. The
component therefore exposes none of them. Detailed build, IAP, SWD and firmware
remediation work belongs exclusively to the firmware project.

## Source-confirmed defects and host mitigations

| Firmware behavior | Consequence | Required host mitigation |
|---|---|---|
| Invalid register bank aliases channel 0 | Wrong physical output changes | Exact schema and runtime validation |
| Unknown reads return stale TX data | Plausible but false values | Never issue unknown reads; exact response sizes |
| ADC timeout can return a stale valid sample | Undetectable loss of freshness | Document limitation; do not promise per-sample freshness |
| I2C ACK is not a semantic or physical-state confirmation | Rejected values can look successful | Validate before writing; treat entity state as commanded/desired and track transport health |
| Error handling and incomplete-write recovery are defective | Persistent faults or repeated 500 ms stalls | Rate-limit recovery probes; invalidate applied-state caches; hardware fault testing |
| Modes silently replace each other | Configured entities fight over a pin | Enforce one owner per endpoint |
| Fixed software PWM | No RTTTL pitch or load-specific frequency | Document about 392 Hz; reject/ignore frequency requests visibly |
| PWM duties 0 and 255 have edge-state defects | Glitch at zero or stale low at full scale | Use digital low/high for encoded extrema; PWM registers only for 1 through 254 |
| PWM/servo depend on main-loop polling | Jitter and stretched pulses under load | Rate-limit I2C work; hardware stress tests |
| Invalid servo values change mode before rejection | Unexpected pulses | Host range validation |
| RGB fill bounds check is wrong | Out-of-bounds RAM access | Enforce `start + count <= configured_count <= 74` |
| RGB brightness math is nonlinear and delayed | Incorrect brightness | Keep firmware scaler at 255; host-scale RGB |
| I2C address writes wear flash and lack confirmation | Address loss or reserved address | Do not expose runtime address mutation |
| Reset/IAP has no safe public updater | Bricking risk | Do not expose `0xFD` or IAP |

## Hardware validation backlog

The following observations must be measured before being promoted from
calculated or source-confirmed behavior to verified device behavior:

1. PWM frequency, high time and jitter at encoded duties 1, 127 and 254; verify
   that encoded 0 and 255 use stable digital low and high.
2. PWM re-entry after digital extrema, ADC, servo and RGB mode changes.
3. Servo pulse width and frame rate at 500, 1500 and 2500 microseconds.
4. PWM and servo behavior while polling ADC and updating 74 LEDs.
5. RGB color order at byte values 0, 127, 128, 254 and 255, plus both LED timing
   modes on representative supported LEDs.
6. Digital input behavior on floating, driven-low and driven-high signals.
7. ADC accuracy and noise at safe known voltages on all six channels.
8. I2C behavior at 100 kHz and 400 kHz, including an interrupted write, hub
   reconnect, injected bus errors and detection of repeating 500 ms stalls.
9. Power sequencing and hub reset when the ESP device and PBHUB do not start or
   restart simultaneously.

## ESPHome scope boundary

ESPHome v2 targets the existing application protocol. It does not expose
firmware mutation, address mutation or reset-to-IAP controls. Firmware changes,
build recovery work and their validation remain in the separate firmware
project.
