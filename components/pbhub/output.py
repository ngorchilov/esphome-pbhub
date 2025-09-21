import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output
from esphome.const import CONF_ID, CONF_NUMBER

from . import CONF_PBHUB, PbHubComponent, pbhub_ns

CODEOWNERS = ["@ngorchilov"]   # optional, but conventional
DEPENDENCIES = ["pbhub"]

PbHubOutput = pbhub_ns.class_("PbHubPWMPin", output.FloatOutput)

# ----- Schema -----
CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend({
    cv.Required("pin"): cv.Schema({
        cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
        cv.Required(CONF_NUMBER): cv.int_range(min=0, max=51),  # slot*10+idx (00..51)
    }),
    cv.GenerateID(CONF_ID): cv.declare_id(PbHubOutput),
})

# ----- Codegen -----
async def to_code(config):
    parent = await cg.get_variable(config["pin"][CONF_PBHUB])
    number = config["pin"][CONF_NUMBER]
    var = cg.new_Pvariable(config[CONF_ID], parent, number)
    await output.register_output(var, config)
