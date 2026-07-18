import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import (
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_ID,
    CONF_OUTPUT_ID,
    CONF_RESTORE_MODE,
)

from . import (
    CONF_NUM_LEDS,
    CONF_PBHUB_ID,
    CONF_SLOT,
    EndpointOwner,
    PbHubComponent,
    pbhub_ns,
    validate_led_count,
    validate_slot,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["pbhub"]

PbHubRGBLight = pbhub_ns.class_("PbHubRGBLight", light.LightOutput)

CONFIG_SCHEMA = light.light_schema(
    PbHubRGBLight,
    light.LightType.RGB,
    default_restore_mode="ALWAYS_OFF",
).extend(
    {
        cv.Required(CONF_PBHUB_ID): cv.use_id(PbHubComponent),
        cv.Required(CONF_SLOT): validate_slot,
        cv.Required(CONF_NUM_LEDS): validate_led_count,
        # An ordinary light change should be one PBHUB fill. Users can opt in
        # to transitions; the parent-wide scheduler still caps their traffic.
        cv.Optional(
            CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
        ): cv.positive_time_period_milliseconds,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB_ID])
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent, config[CONF_SLOT])
    cg.add(
        parent.claim_endpoint(
            config[CONF_SLOT] * 10 + 1,
            EndpointOwner.RGB,
            str(config[CONF_ID]),
        )
    )
    cg.add(parent.register_recovery_client(var))
    cg.add(var.set_led_count(config[CONF_NUM_LEDS]))
    # ESPHome keeps this validated field as an EStr but its enum table contains
    # MockObj values with overloaded equality, so compare the raw mode name.
    startup_off = str(config[CONF_RESTORE_MODE]) in (
        "ALWAYS_OFF",
        "RESTORE_AND_OFF",
    )
    cg.add(var.set_startup_off(startup_off))
    await light.register_light(var, config)
