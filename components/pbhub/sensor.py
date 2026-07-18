import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_CHANNEL,
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
    UNIT_EMPTY,
)

from . import (
    CONF_PBHUB_ID,
    CONF_SIGNAL,
    EndpointOwner,
    PbHubComponent,
    SIGNAL_A_INDEX,
    pbhub_ns,
    validate_channel,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["pbhub"]

PbHubADC = pbhub_ns.class_("PbHubADC", sensor.Sensor, cg.PollingComponent)


def _reject_signal(config):
    if CONF_SIGNAL in config:
        raise cv.Invalid(
            "PBHUB ADC always uses signal A; remove the signal option",
            path=[CONF_SIGNAL],
        )
    return config


CONFIG_SCHEMA = cv.All(
    _reject_signal,
    sensor.sensor_schema(
        PbHubADC,
        unit_of_measurement=UNIT_EMPTY,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            cv.Required(CONF_PBHUB_ID): cv.use_id(PbHubComponent),
            cv.Required(CONF_CHANNEL): validate_channel,
        }
    )
    .extend(cv.polling_component_schema("1s")),
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent, config[CONF_CHANNEL])
    cg.add(
        parent.claim_endpoint(
            config[CONF_CHANNEL],
            SIGNAL_A_INDEX,
            EndpointOwner.ADC,
            str(config[CONF_ID]),
        )
    )
    await sensor.register_sensor(var, config)
    await cg.register_component(var, config)
