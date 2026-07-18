import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_OUTPUT_ID

from . import (
    CONF_NUM_LEDS,
    CONF_PBHUB_ID,
    CONF_SLOT,
    PbHubComponent,
    pbhub_ns,
    validate_led_count,
    validate_slot,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["pbhub"]

PbHubRGBLight = pbhub_ns.class_("PbHubRGBLight", light.LightOutput)

CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(PbHubRGBLight),
        cv.Required(CONF_PBHUB_ID): cv.use_id(PbHubComponent),
        cv.Required(CONF_SLOT): validate_slot,
        cv.Required(CONF_NUM_LEDS): validate_led_count,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB_ID])
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent, config[CONF_SLOT])
    cg.add(var.set_led_count(config[CONF_NUM_LEDS]))
    await light.register_light(var, config)
