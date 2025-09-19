# ESPHome PBHUB Component

Custom ESPHome component for the M5Stack PBHUB / PortHub.

## Installation

You don’t need to copy sources manually.  
Instead, reference this repository directly in your ESPHome YAML:

```yaml
external_components:
    - source: github://ngorchilov/esphome-pbhub@main
      refresh: 0s
      components: [pbhub]
```

## Example Usage

```yaml
switch:
    - platform: gpio
      pin:
          pbhub: pb_hub
          number: 30
      name: 'Relay'
```

## Features

-   Digital GPIO (per-slot A/B pins)
-   Analog input
-   Servo control
