import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_INVERTED, CONF_PIN

from . import (
    CONF_PBHUB_ID,
    EndpointOwner,
    PbHubComponent,
    pbhub_ns,
    validate_endpoint,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["pbhub"]

PbHubBinarySensor = pbhub_ns.class_(
    "PbHubBinarySensor", binary_sensor.BinarySensor, cg.PollingComponent
)

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(PbHubBinarySensor)
    .extend(
        {
            cv.Required(CONF_PBHUB_ID): cv.use_id(PbHubComponent),
            cv.Required(CONF_PIN): validate_endpoint,
            cv.Optional(CONF_INVERTED, default=False): cv.boolean,
        }
    )
    .extend(cv.polling_component_schema("100ms"))
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent, config[CONF_PIN])
    cg.add(
        parent.claim_endpoint(
            config[CONF_PIN], EndpointOwner.DIGITAL_INPUT, str(config[CONF_ID])
        )
    )
    await binary_sensor.register_binary_sensor(var, config)
    await cg.register_component(var, config)
