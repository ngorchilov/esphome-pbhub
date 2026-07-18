import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ICON_EMPTY, UNIT_EMPTY

from . import (
    CONF_PBHUB_ID,
    CONF_SLOT,
    EndpointOwner,
    PbHubComponent,
    pbhub_ns,
    validate_slot,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["pbhub"]

PbHubADC = pbhub_ns.class_("PbHubADC", sensor.Sensor, cg.PollingComponent)

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        unit_of_measurement=UNIT_EMPTY,
        icon=ICON_EMPTY,
        accuracy_decimals=0,
    )
    .extend(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(PbHubADC),
            cv.Required(CONF_PBHUB_ID): cv.use_id(PbHubComponent),
            cv.Required(CONF_SLOT): validate_slot,
        }
    )
    .extend(cv.polling_component_schema("1s"))
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent, config[CONF_SLOT])
    cg.add(
        parent.claim_endpoint(
            config[CONF_SLOT] * 10, EndpointOwner.ADC, str(config[CONF_ID])
        )
    )
    await sensor.register_sensor(var, config)
    await cg.register_component(var, config)
