import esphome.config_validation as cv
from esphome.components import output
from esphome.const import (
    CONF_GAIN,
    CONF_ID,
    CONF_INVERTED,
    CONF_MAX_POWER,
    CONF_MIN_POWER,
    CONF_OUTPUT,
    CONF_PLATFORM,
)


def _pbhub_output_configs(full_config):
    return {
        str(entry[CONF_ID]): entry
        for entry in full_config.get("output", [])
        if entry.get(CONF_PLATFORM) == "pbhub"
    }


def _transformed_level(config, level):
    if level != 0.0 or not config.get(output.CONF_ZERO_MEANS_ZERO, False):
        minimum = config.get(CONF_MIN_POWER, 0.0)
        maximum = config.get(CONF_MAX_POWER, 1.0)
        level = level * (maximum - minimum) + minimum
    if config.get(CONF_INVERTED, False):
        level = 1.0 - level
    return min(max(level, 0.0), 1.0)


def _reject_power_transform_actions(value, output_ids, path=()):
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = (*path, key)
            action = str(key)
            if action in (
                "output.set_min_power",
                "output.set_max_power",
            ) and isinstance(child, dict):
                output_id = str(child.get(CONF_ID))
                if output_id in output_ids:
                    raise cv.FinalExternalInvalid(
                        f"PBHUB fixed-tone RTTTL output '{output_id}' cannot "
                        f"be targeted by {action}; changing its power range "
                        "would invalidate the configured audible duty",
                        path=[cv.ROOT_CONFIG_PATH, *child_path, CONF_ID],
                    )
            _reject_power_transform_actions(child, output_ids, child_path)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _reject_power_transform_actions(child, output_ids, (*path, index))


def validate_rtttl_consumers(
    full_config,
    output_modes,
    pwm_mode,
    servo_mode,
):
    output_configs = _pbhub_output_configs(full_config)
    consumers = {}

    for index, entry in enumerate(full_config.get("rtttl", [])):
        output_id = str(entry.get(CONF_OUTPUT))
        mode = output_modes.get(output_id)
        if mode == servo_mode:
            raise cv.FinalExternalInvalid(
                f"PBHUB output '{output_id}' cannot be used by RTTTL: servo "
                "mode writes direct pulse widths rather than PWM duty",
                path=[cv.ROOT_CONFIG_PATH, "rtttl", index, CONF_OUTPUT],
            )
        if mode != pwm_mode:
            continue

        previous = consumers.get(output_id)
        if previous is not None:
            raise cv.FinalExternalInvalid(
                f"PBHUB fixed-tone RTTTL output '{output_id}' is referenced by "
                f"more than one RTTTL component (rtttl[{previous}] and "
                f"rtttl[{index}])",
                path=[cv.ROOT_CONFIG_PATH, "rtttl", index, CONF_OUTPUT],
            )
        consumers[output_id] = index

        output_config = output_configs[output_id]
        if output_config.get(CONF_INVERTED, False):
            raise cv.FinalExternalInvalid(
                f"PBHUB fixed-tone RTTTL output '{output_id}' requires "
                "inverted: false so pauses and note gaps drive low",
                path=[cv.ROOT_CONFIG_PATH, "rtttl", index, CONF_OUTPUT],
            )
        if not output_config.get(output.CONF_ZERO_MEANS_ZERO, False):
            raise cv.FinalExternalInvalid(
                f"PBHUB fixed-tone RTTTL output '{output_id}' requires "
                "zero_means_zero: true so pauses and note gaps remain low",
                path=[cv.ROOT_CONFIG_PATH, "rtttl", index, CONF_OUTPUT],
            )

        minimum = output_config.get(CONF_MIN_POWER, 0.0)
        maximum = output_config.get(CONF_MAX_POWER, 1.0)
        if minimum > maximum:
            raise cv.FinalExternalInvalid(
                f"PBHUB fixed-tone RTTTL output '{output_id}' requires "
                "min_power to be less than or equal to max_power",
                path=[cv.ROOT_CONFIG_PATH, "rtttl", index, CONF_OUTPUT],
            )

        transformed_gain = _transformed_level(output_config, entry[CONF_GAIN])
        duty = int(transformed_gain * 255.0 + 0.5)
        if not 1 <= duty <= 254:
            raise cv.FinalExternalInvalid(
                f"PBHUB fixed-tone RTTTL output '{output_id}' gain and output "
                "power transforms must produce PWM duty 1..254, not "
                f"{duty}; duty 0 is silent and duty 255 is constant high",
                path=[cv.ROOT_CONFIG_PATH, "rtttl", index, CONF_GAIN],
            )

    _reject_power_transform_actions(full_config, set(consumers))
