import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_NUMBER,
    UNIT_EMPTY,
    ICON_EMPTY,
)

from . import CONF_PBHUB, PbHubComponent, pbhub_ns

CODEOWNERS = ["@ngorchilov"]   # optional, but conventional
DEPENDENCIES = ["pbhub"]

PbHubADC = pbhub_ns.class_("PbHubADC", sensor.Sensor, cg.PollingComponent)

# ----- Schema -----
CONFIG_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_EMPTY,
    icon=ICON_EMPTY,
    accuracy_decimals=0,
).extend({
    cv.GenerateID(CONF_ID): cv.declare_id(PbHubADC),
    cv.Required("pin"): cv.Schema({
        cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
        cv.Required(CONF_NUMBER): cv.int_range(min=0, max=51),  # slot*10+idx
    }),
})

# ----- Codegen -----
async def to_code(config):
    parent = await cg.get_variable(config["pin"][CONF_PBHUB])
    number = config["pin"][CONF_NUMBER]
    var = cg.new_Pvariable(config[CONF_ID], parent, number)
    await sensor.register_sensor(var, config)
    await cg.register_component(var, config)