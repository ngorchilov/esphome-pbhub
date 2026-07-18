import esphome.codegen as cg
from esphome.components import output
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MODE, CONF_PIN

from . import (
    CONF_PBHUB_ID,
    PbHubComponent,
    pbhub_ns,
    validate_endpoint,
    validate_output_mode,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["pbhub"]

PbHubPWMOutput = pbhub_ns.class_("PbHubPWMPin", output.FloatOutput)

CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend(
    {
        cv.Required(CONF_ID): cv.declare_id(PbHubPWMOutput),
        cv.Required(CONF_PBHUB_ID): cv.use_id(PbHubComponent),
        cv.Required(CONF_PIN): validate_endpoint,
        cv.Required(CONF_MODE): validate_output_mode,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent, config[CONF_PIN])
    await output.register_output(var, config)
