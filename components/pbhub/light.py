import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_ID, CONF_SLOT

from . import CONF_PBHUB, PbHubComponent, pbhub_ns

CODEOWNERS = ["@ngorchilov"]   # optional, but conventional
DEPENDENCIES = ["pbhub"]

PbHubRGBLight = pbhub_ns.class_("PbHubRGBLight", light.LightOutput)

# ----- Schema -----
CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend({
    cv.Required("slot"): cv.int_range(min=0, max=51),  # slot number (0..51)
    cv.Optional("led_count", default=1): cv.int_range(min=1),
    cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
    cv.GenerateID(CONF_ID): cv.declare_id(PbHubRGBLight),
})

# ----- Codegen -----
async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    cg.add(var.set_slot(config["slot"]))
    cg.add(var.set_led_count(config["led_count"]))
    await light.register_light(var, config)