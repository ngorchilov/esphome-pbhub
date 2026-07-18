import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_CHANNEL, CONF_ID

from . import (
    CONF_PBHUB_ID,
    CONF_SIGNAL,
    EndpointOwner,
    PbHubComponent,
    pbhub_ns,
    validate_channel,
    validate_signal,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["pbhub"]

PbHubSwitch = pbhub_ns.class_(
    "PbHubSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = switch.switch_schema(
    PbHubSwitch, default_restore_mode="ALWAYS_OFF"
).extend(
    {
        cv.Required(CONF_PBHUB_ID): cv.use_id(PbHubComponent),
        cv.Required(CONF_CHANNEL): validate_channel,
        cv.Required(CONF_SIGNAL): validate_signal,
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB_ID])
    signal = config[CONF_SIGNAL]
    var = await switch.new_switch(config, parent, config[CONF_CHANNEL], signal)
    cg.add(
        parent.claim_endpoint(
            config[CONF_CHANNEL],
            signal,
            EndpointOwner.DIGITAL_OUTPUT,
            str(config[CONF_ID]),
        )
    )
    cg.add(parent.register_recovery_client(var))
    await cg.register_component(var, config)
