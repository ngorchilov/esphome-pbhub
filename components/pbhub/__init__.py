import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import i2c
from esphome.const import (
    CONF_CHANNEL,
    CONF_ID,
    CONF_IDLE_LEVEL,
    CONF_MAX_LEVEL,
    CONF_MIN_LEVEL,
    CONF_MODE,
    CONF_NUM_LEDS,
    CONF_OUTPUT,
    CONF_OUTPUT_ID,
    CONF_PLATFORM,
    CONF_TRANSITION_LENGTH,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

CONF_PBHUB_ID = "pbhub_id"
CONF_SIGNAL = "signal"
CONF_LED_TIMING_MODE = "led_timing_mode"

OUTPUT_MODE_PWM = "pwm"
OUTPUT_MODE_SERVO = "servo"
SERVO_MIN_LEVEL = 0.025
SERVO_MAX_LEVEL = 0.125

SIGNAL_A_INDEX = 0
SIGNAL_B_INDEX = 1
SIGNAL_INDEX = {"a": SIGNAL_A_INDEX, "b": SIGNAL_B_INDEX}

pbhub_ns = cg.esphome_ns.namespace("pbhub")
PbHubComponent = pbhub_ns.class_("PbHubComponent", cg.Component, i2c.I2CDevice)
EndpointOwner = pbhub_ns.enum("EndpointOwner", is_class=True)


def validate_channel(value):
    value = cv.int_(value)
    if not 0 <= value <= 5:
        raise cv.Invalid("PBHUB channel must be an integer from 0 to 5")
    return value


validate_signal = cv.enum(SIGNAL_INDEX)


def signal_index(value):
    return SIGNAL_INDEX[str(value)]


def validate_led_count(value):
    value = cv.int_(value)
    if not 1 <= value <= 74:
        raise cv.Invalid("PBHUB num_leds must be an integer from 1 to 74")
    return value


def validate_led_timing_mode(value):
    value = cv.int_(value)
    if value not in (0, 1):
        raise cv.Invalid("PBHUB LED timing mode must be 0 or 1")
    return value


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PbHubComponent),
            cv.Optional(CONF_LED_TIMING_MODE): validate_led_timing_mode,
        }
    )
    .extend(i2c.i2c_device_schema(0x61))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    if CONF_LED_TIMING_MODE in config:
        cg.add(var.set_led_timing_mode(config[CONF_LED_TIMING_MODE]))


def _path_text(path):
    text = ""
    for part in path:
        if isinstance(part, int):
            text += f"[{part}]"
        else:
            text += ("." if text else "") + str(part)
    return text


def _collect_endpoint_claims(full_config):
    specs = (
        (
            "output",
            CONF_SIGNAL,
            lambda entry: signal_index(entry[CONF_SIGNAL]),
            lambda entry: (
                "servo output"
                if entry.get(CONF_MODE) == OUTPUT_MODE_SERVO
                else "PWM output"
            ),
        ),
        (
            "sensor",
            CONF_CHANNEL,
            lambda _entry: 0,
            lambda _entry: "ADC sensor",
        ),
        (
            "light",
            CONF_CHANNEL,
            lambda _entry: 1,
            lambda _entry: "RGB light",
        ),
        (
            "binary_sensor",
            CONF_SIGNAL,
            lambda entry: signal_index(entry[CONF_SIGNAL]),
            lambda _entry: "digital input",
        ),
        (
            "switch",
            CONF_SIGNAL,
            lambda entry: signal_index(entry[CONF_SIGNAL]),
            lambda _entry: "digital output",
        ),
    )
    claims = []
    for rank, (domain, field, signal_fn, label_fn) in enumerate(specs):
        for index, entry in enumerate(full_config.get(domain, [])):
            if entry.get(CONF_PLATFORM) != "pbhub":
                continue
            entity_id = entry.get(CONF_ID, entry.get(CONF_OUTPUT_ID, "<generated>"))
            path = [domain, index]
            claims.append(
                {
                    "hub": str(entry[CONF_PBHUB_ID]),
                    "channel": entry[CONF_CHANNEL],
                    "signal": signal_fn(entry),
                    "rank": rank,
                    "index": index,
                    "path": path,
                    "path_text": _path_text(path),
                    "field": field,
                    "label": label_fn(entry),
                    "entity_id": str(entity_id),
                }
            )
    return sorted(
        claims,
        key=lambda claim: (
            claim["hub"],
            claim["channel"],
            claim["signal"],
            claim["rank"],
            claim["index"],
        ),
    )


def _pbhub_output_modes(full_config):
    return {
        str(entry[CONF_ID]): entry[CONF_MODE]
        for entry in full_config.get("output", [])
        if entry.get(CONF_PLATFORM) == "pbhub"
    }


def _reject_servo_output_actions(value, servo_output_ids, path=()):
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = (*path, key)
            action = str(key)
            if action in (
                "output.set_min_power",
                "output.set_max_power",
                "output.turn_on",
            ) and isinstance(child, dict):
                output_id = str(child.get(CONF_ID))
                if output_id in servo_output_ids:
                    raise cv.FinalExternalInvalid(
                        f"PBHUB servo output '{output_id}' cannot be targeted "
                        f"by {action}; servo transforms and pulse range are fixed",
                        path=[cv.ROOT_CONFIG_PATH, *child_path, CONF_ID],
                    )
            _reject_servo_output_actions(child, servo_output_ids, child_path)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _reject_servo_output_actions(child, servo_output_ids, (*path, index))


def _validate_output_consumers(full_config):
    output_modes = _pbhub_output_modes(full_config)
    if not output_modes:
        return

    for index, entry in enumerate(full_config.get("rtttl", [])):
        output_id = str(entry.get(CONF_OUTPUT))
        if output_id in output_modes:
            raise cv.FinalExternalInvalid(
                f"PBHUB output '{output_id}' cannot be used by RTTTL: stock "
                "firmware does not support dynamic PWM frequency",
                path=[cv.ROOT_CONFIG_PATH, "rtttl", index, CONF_OUTPUT],
            )

    servo_consumers = {}
    for index, entry in enumerate(full_config.get("servo", [])):
        output_id = str(entry.get(CONF_OUTPUT))
        mode = output_modes.get(output_id)
        if mode is not None and mode != OUTPUT_MODE_SERVO:
            raise cv.FinalExternalInvalid(
                f"PBHUB output '{output_id}' in mode '{mode}' cannot be used "
                "by ESPHome servo: fixed-frequency PWM is not the firmware's "
                "50 Hz servo generator",
                path=[cv.ROOT_CONFIG_PATH, "servo", index, CONF_OUTPUT],
            )
        if mode != OUTPUT_MODE_SERVO:
            continue

        existing_index = servo_consumers.get(output_id)
        if existing_index is not None:
            raise cv.FinalExternalInvalid(
                f"PBHUB servo output '{output_id}' is referenced by more than "
                f"one ESPHome servo (servo[{existing_index}] and servo[{index}])",
                path=[cv.ROOT_CONFIG_PATH, "servo", index, CONF_OUTPUT],
            )
        servo_consumers[output_id] = index

        for field in (CONF_MIN_LEVEL, CONF_IDLE_LEVEL, CONF_MAX_LEVEL):
            level = entry[field]
            if not SERVO_MIN_LEVEL <= level <= SERVO_MAX_LEVEL:
                raise cv.FinalExternalInvalid(
                    f"PBHUB servo '{entry[CONF_ID]}' {field} must be between "
                    "2.5% and 12.5% so firmware pulses remain within "
                    "500..2500 us",
                    path=[cv.ROOT_CONFIG_PATH, "servo", index, field],
                )

        transition = entry[CONF_TRANSITION_LENGTH]
        if transition.total_milliseconds != 0:
            raise cv.FinalExternalInvalid(
                f"PBHUB servo '{entry[CONF_ID]}' requires transition_length: "
                "0s because ESPHome software transitions would write I2C on "
                "every loop pass",
                path=[
                    cv.ROOT_CONFIG_PATH,
                    "servo",
                    index,
                    CONF_TRANSITION_LENGTH,
                ],
            )

    servo_output_ids = {
        output_id
        for output_id, mode in output_modes.items()
        if mode == OUTPUT_MODE_SERVO
    }
    _reject_servo_output_actions(full_config, servo_output_ids)


def _final_validate(config):
    full_config = fv.full_config.get()
    data_key = "pbhub.final_validation_complete"
    if full_config.data.get(data_key):
        return config

    _validate_output_consumers(full_config)

    claimed = {}
    for claim in _collect_endpoint_claims(full_config):
        key = (claim["hub"], claim["channel"], claim["signal"])
        existing = claimed.get(key)
        if existing is None:
            claimed[key] = claim
            continue

        raise cv.FinalExternalInvalid(
            f"PBHUB channel {claim['channel']} signal "
            f"{'AB'[claim['signal']]} on hub '{claim['hub']}' is claimed by "
            f"both {existing['label']} '{existing['entity_id']}' at "
            f"{existing['path_text']} and {claim['label']} "
            f"'{claim['entity_id']}' at {claim['path_text']}",
            path=[cv.ROOT_CONFIG_PATH, *claim["path"], claim["field"]],
        )

    full_config.data[data_key] = True
    return config


FINAL_VALIDATE_SCHEMA = _final_validate
