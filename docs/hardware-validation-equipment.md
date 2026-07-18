# PBHUB v2 Hardware Validation Equipment

This document lists the hardware, instruments and fixtures needed to execute the
real-hardware validation matrix in the
[v2 implementation plan](v2-implementation-plan.md#required-real-hardware-matrix).
It is an acquisition and preparation checklist, not a record of completed tests.

The target device is the current STM32-based
[M5Stack Unit PbHub v1.1, SKU U041-B](https://docs.m5stack.com/en/unit/pbhub_1.1).
Do not substitute the earlier ATmega328-based U041 model. Every unit must report
application firmware version `2` before feature testing begins.

## Validation levels

| Level | PBHUB quantity | Coverage |
|---|---:|---|
| Single-hub characterization | 1 | Digital, ADC, PWM, servo, RGB, direct-bus timing and basic disconnect/recovery tests |
| Complete Phase 10 matrix | 3 | All single-hub tests plus two physical buses, distinct-address hubs on one bus and equal-address hubs behind a TCA9548A |

One PBHUB is enough to start most functional measurements. Three are needed to
finish the topology matrix without repeatedly rewriting persistent addresses:

1. one primary unit at the default address `0x61`;
2. a second unit at `0x61` for equal-address TCA9548A isolation; and
3. one separately provisioned unit at another valid address, such as `0x62`, for
   the shared-bus distinct-address test.

The component deliberately provides no address-changing feature. Obtaining or
provisioning the alternate-address unit is therefore a prerequisite that needs a
separately reviewed procedure before the complete matrix can run. Rewriting one
of two units between scenarios is not the default plan because firmware address
writes erase flash, provide no confirmation and can persist an unusable address.

## Devices and topology hardware

| Item | Quantity | Required characteristics | Purpose |
|---|---:|---|---|
| ESP32 development board | 1 | Supported by ESP-IDF; exposes at least four suitable GPIOs for two independent I2C controllers | Canonical runtime target, direct I2C and two-physical-bus tests |
| Unit PbHub v1.1 U041-B | 1 to begin; 3 for the complete matrix | Stock application firmware reporting version `2`; two default-address units and one separately provisioned alternate-address unit | Device under test and multi-hub topologies |
| TCA9548A breakout | 1 | 3.3 V-compatible control side, at least two exposed downstream channels and 400 kHz operation | Virtual-channel routing and isolation of two hubs at `0x61` |
| Passive I2C fan-out or terminal breakout | 1 | Individually accessible 5 V, GND, SDA and SCL; no hidden level conversion | Shared-bus wiring, probing and controlled disconnection |
| Independently switchable power fixture | 1 | Controller and PBHUB power can be enabled separately or together; current limiting available | Controller-first, hub-first, simultaneous startup, hub-only reset and reconnect |
| I2C fault-injection fixture | 1 | Inline access to SDA/SCL plus a transaction-triggerable open-drain injector; must never actively drive an I2C line high | Reproducibly interrupted transactions, stuck-low/bus-error tests and firmware recovery-stall observation |

The [ESP-IDF I2C documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html)
describes the ESP32's two I2C controllers and the 100 kHz and 400 kHz modes used
by the matrix. The selected
[TCA9548A](https://www.ti.com/product/TCA9548A) board must preserve 3.3 V logic
levels and support both rates.

## Cables, breakouts and protection

The U041-B package contains one 20 cm HY2.0-4P cable, which is normally consumed
by its I2C input. Extra output-side cables or breakouts are required. M5Stack's
[connector definition](https://docs.m5stack.com/en/learn/intro#interface-protocols),
the PBHUB schematic and the firmware map together give this test-fixture mapping:

| PBHUB signal | PORT.B wire | M5Stack name | Schematic net |
|---|---|---|---|
| A | White | IO2 | `INn` |
| B | Yellow | IO1 | `OUTn` |

The A/B mapping is derived from those official artifacts; A and B are the names
used by this component rather than labels printed on the connector.

Acquire or prepare:

- at least two HY2.0-4P PORT.B-to-terminal/bare-wire breakouts for the concurrent
  ADC, servo and 74-LED RGB stress test;
- preferably six output breakouts, one for every PBHUB channel, to avoid moving
  probes and stimulus wiring between channel-coverage tests;
- one PORT.A/I2C-to-terminal, bare-wire or Dupont adapter for every PBHUB that
  will be connected simultaneously to a pin-header ESP32 or multiplexer;
- enough HY2.0-4P I2C cables and branch adapters to connect three hubs and the
  multiplexer in the required topologies;
- a breadboard or guarded terminal fixture, jumper wires and a common-ground
  distribution point;
- 1 kOhm to 10 kOhm series-resistor options for externally driven ADC and
  digital inputs, plus an I2C pull-up assortment for measured bus tuning; and
- data-and-ground-only load leads for externally powered LEDs and an optional
  servo. Their PBHUB 5 V conductor must be positively isolated so that two 5 V
  supplies cannot backfeed each other.

The board exposes 5 V on each output connector, but its MCU signals and ADC are
3.3 V. No externally driven signal may leave the `0..3.3 V` range. The
[official schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/579/SHC_UNIT_PBHUB_v1.1.pdf)
shows one shared 5 V rail and no per-port current limiter. The official material
reviewed provides no per-port or aggregate load-current rating. Long LED strips
and an optional servo must therefore use a separately current-limited load
supply, sized from the actual peripheral datasheets, with a common ground and no
competing supply connection.

## Measurement instruments

| Instrument | Minimum useful capability | Measurements |
|---|---|---|
| Digital multimeter | DC voltage and continuity; enough resolution to verify the selected ADC stimulus points | ADC reference values, rails, continuity and brownout diagnosis |
| Oscilloscope | 2 analog channels minimum, 4 preferred; at least 20 MHz bandwidth, 100 MS/s and single-shot/deep-memory capture | PWM duty/frequency/jitter, servo pulse/frame timing, RGB waveform, I2C rise time and output behavior during faults |
| Logic analyzer or mixed-signal equivalent | 4 digital channels minimum, 8 preferred; 3.3 V-compatible; at least 10 MS/s, 25 MS/s preferred; I2C decode and at least five seconds of capture or streaming | Register-level I2C evidence, ACKs, clock stretching, repeated 500 ms stalls, digital-versus-PWM register selection and correlation with outputs |
| Adjustable ADC source | Stable, current-limited `0..3.3 V`; a calibrated source, DAC/reference fixture or divider verified by the multimeter | Known-zero and known-voltage ADC tests on signal A of all channels |
| Programmable 3.3 V pulse source | Adjustable level, period and pulse width; function generator or second timer-driven MCU | Digital input state, inversion and shortest reliably detected pulse |
| Regulated 5 V load supply | Current-limited and sized for the chosen 5 V LED strip plus an optional servo at their documented maximum loads | 74-pixel RGB and optional servo tests without loading the PBHUB/controller rail |

A mixed-signal oscilloscope may satisfy both scope and logic-analyzer rows if it
has sufficient digital channels, capture depth and protocol decoding. The
[Saleae sampling guidance](https://www.saleae.com/support/logic-software/capturing-data/what-sample-rate-is-required)
is a useful lower-bound reference; the higher preferred rates above leave margin
for RGB timing and short I2C faults rather than only decoding steady-state I2C.
A second 3.3 V MCU can serve as both the programmable input-pulse source and the
trigger source for open-drain fault-injection transistors.

## Stimuli and loads

| Peripheral | Quantity and selection | Tests enabled |
|---|---|---|
| Protected digital-input fixture | 1; selectable GND/3.3 V or pushbuttons through series resistance | All 12 channel/signal inputs, inversion and stable low/high states |
| Three-channel addressable RGB strip, timing mode 0 | At least 74 pixels from a verified 5 V WS2812 or RGB SK6812 family; not RGBW | GRB order, off, host scaling, counts 1/intermediate/74 and concurrent-load test |
| Three-channel addressable RGB specimen, timing mode 1 | At least one short chain from the published SK6822, APA106 or PL9823 families | Global LED timing mode 1 verification |
| Optional hobby servo | 1; accepts a 3.3 V control signal, unloaded initially, with documented supply and pulse ranges covering the intended mechanical test points | Additional end-to-end motion and detach check after the required electrical validation |
| Optional visual digital load | LED with a correctly calculated series resistor or another high-impedance logic indicator | Convenient digital-output indication; oscilloscope/logic evidence remains authoritative |

The long RGB strip and mode-1 specimen must have known part numbers and
manufacturer timing/current specifications. RGBW devices are unsuitable because
the firmware sends only 24-bit GRB data. The implementation plan's servo
acceptance criteria are electrical and require the oscilloscope, not a physical
servo. If the optional servo is used, verify the 500 us and 2500 us extremes
electrically before attaching it; do not drive a mechanism into its stops merely
to satisfy the pulse-width test.

## Coverage map

| Validation area | Required equipment |
|---|---|
| Digital | One PBHUB, output breakouts, protected static input fixture, programmable pulse source, scope or logic analyzer |
| ADC | One PBHUB, adjustable `0..3.3 V` source, series protection, multimeter and output breakouts |
| PWM | One PBHUB, oscilloscope and logic analyzer/I2C decode |
| Servo | One PBHUB and oscilloscope; optional documented-range servo and external 5 V supply for an additional motion check |
| RGB | One PBHUB, both timing-family specimens, 74-pixel strip, external 5 V load supply, oscilloscope and logic analyzer |
| Direct bus and two physical buses | One ESP32 exposing two I2C controllers, at least two PBHUBs, independent wiring and logic analyzer |
| Distinct-address shared bus | Two PBHUBs, including the separately provisioned alternate-address unit, passive fan-out and logic analyzer |
| Equal-address virtual channels | Two default-address PBHUBs, TCA9548A and logic analyzer |
| Power and recovery | Independent power fixture, current limiting, I2C fault injector, scope and logic analyzer |

## Preparation blockers and test-specific decisions

Equipment acquisition alone does not resolve these points. They must be settled
when the detailed Phase 10 procedure is written:

1. Define and review how the alternate-address PBHUB will be provisioned without
   adding address mutation to this component.
2. Record the exact RGB part numbers and use at least one three-channel device
   from each published timing-mode family row.
3. Define ADC voltage points, source accuracy and acceptance tolerances before
   measuring accuracy or noise.
4. Define digital pulse widths, polling intervals, repetition count and the pass
   criterion for detection probability.
5. Define PWM/servo capture duration and jitter metrics before characterizing
   minimum, maximum or percentile behavior.
6. Decide whether mode-re-entry tests use sequential purpose-built
   configurations or an internal hardware-test build. Normal ownership
   validation intentionally prevents two public entities from fighting for the
   same channel/signal.
7. Decide whether the rejected-RGB-command no-traffic assertion remains a host
   test or gains an internal hardware-test hook. The public schema cannot
   generate an invalid range.
8. Ensure every externally driven line is high-impedance before a hub is powered
   down, or intentionally characterize and current-limit any back-power path.
9. Record the controller board, PBHUB hardware revision, observed firmware byte,
   peripheral part numbers, supply limits and instrument capture settings with
   the eventual measurements. Do not publish serial numbers or deployment
   identifiers.

## Not required for this validation

- SWD programmers, bootloader tools and firmware-flashing fixtures belong to the
  separate firmware-remediation project.
- ESP8266 and ESP32-S3 boards are outside the v2 support matrix.
- An Arduino runtime setup is not required unless a separate Arduino hardware
  validation pass is later approved.
- A fourth expendable PBHUB is useful as a campaign spare, but it is not needed
  to complete the matrix.
