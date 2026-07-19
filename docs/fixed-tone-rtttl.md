# Fixed-tone RTTTL compatibility

This document applies only to the `v2-rtttl` branch. Canonical `v2` rejects
RTTTL on PBHUB outputs because application protocol version 2 cannot change PWM
frequency.

The compatibility branch adds a PBHUB-side adapter for ESPHome's standard RTTTL
component. The parser, YAML schema, actions, condition and callback remain the
built-in ESPHome 2026.7.0 implementation. PBHUB PWM interprets note-frequency
requests as rhythm boundaries without changing frequency. Every non-pause note
therefore drives the output at `gain` and a passive buzzer sounds at the
firmware's fixed calculated nominal frequency near 392.16 Hz.

## Installation

Load the PBHUB branch normally:

```yaml
external_components:
  - source: github://ngorchilov/esphome-pbhub@v2-rtttl
    components: [pbhub]
```

## Configuration

Use an ordinary PBHUB PWM output:

```yaml
output:
  - platform: pbhub
    id: buzzer_output
    pbhub_id: hub
    channel: 1
    signal: b
    mode: pwm
    zero_means_zero: true

rtttl:
  id: buzzer_player
  output: buzzer_output
  gain: 5%
  on_finished_playback:
    - logger.log: Buzzer alert finished
```

PBHUB `mode: servo` remains invalid as an RTTTL output. Servo mode represents
direct pulse widths, not PWM duty.

The standard automation surface remains available:

```yaml
script:
  - id: play_alert
    then:
      - rtttl.play:
          id: buzzer_player
          rtttl: alert:d=16,o=5,b=180:c,f,p,c6.
      - if:
          condition:
            rtttl.is_playing:
              id: buzzer_player
          then:
            - rtttl.stop:
                id: buzzer_player
```

## Playback contract

- RTTTL name, tempo, default duration, explicit duration, octave, sharps, dotted
  notes and pauses are parsed normally.
- Pitch tokens do not change the float output's frequency. `c`, `f` and `c6` in
  the example therefore sound identical on PBHUB.
- Every tone token drives `set_level(gain)`; a pause drives level zero.
- Consecutive tone tokens receive one 10 ms low gap. For a changed pitch token,
  PBHUB defers the next duty write nonblockingly while ESPHome's note schedule
  continues. ESPHome already inserts the equivalent gap for repeated identical
  notes. A tone after an explicit pause needs no extra gap.
- A non-pause token whose entire scheduled duration is 10 ms or less can be
  consumed by that gap and may not produce an audible pulse. Very short tokens
  can also be coalesced to the most recent desired state while the gap is
  pending.
- Requested note frequencies are ignored without per-note warnings.
- `rtttl.play`, `rtttl.stop`, `rtttl.is_playing`, `gain` and
  `on_finished_playback` retain their ESPHome 2026.7.0 interfaces.

Only `PbHubPWMOutput` changes behavior. RTTTL connected to another frequency-aware
float output, or to a `speaker:`, retains ESPHome's normal pitched playback.

For reliable silence and articulation, a PBHUB RTTTL output must use
`inverted: false` and `zero_means_zero: true`. The effective gain after output
power transforms must encode to firmware PWM duty `1..254`; duty 0 is silent and
duty 255 is constant high. The configured `min_power` must not exceed
`max_power`, and runtime `output.set_min_power`/`output.set_max_power` actions
cannot target an RTTTL-bound PBHUB output because they would invalidate this
check. Configuration validation enforces these conditions and permits only one
RTTTL component per PBHUB output.

ESPHome 2026.7.0's built-in RTTTL validator has a fixed whitelist of native PWM
outputs, so it logs a generic warning that RTTTL is not known to work with this
external output type. The warning is expected here: the branch implements and
tests the required `update_frequency()` integration while deliberately ignoring
the requested value.

Playback timing and audibility on real PBHUB hardware remain to be tested. The
firmware implements PWM in software, so I2C traffic can affect timing and jitter.
Each synthetic changed-note boundary adds a low write followed by a duty write;
rapid RTTTL playback therefore also increases bus traffic.
