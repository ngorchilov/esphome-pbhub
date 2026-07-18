import esphome.codegen as cg
from esphome.components import output
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INVERTED,
    CONF_MAX_POWER,
    CONF_MIN_POWER,
    CONF_MODE,
    CONF_PIN,
)

from . import (
    CONF_PBHUB_ID,
    EndpointOwner,
    OUTPUT_MODE_PWM,
    OUTPUT_MODE_SERVO,
    PbHubComponent,
    pbhub_ns,
    validate_endpoint,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["pbhub"]

PbHubPWMOutput = pbhub_ns.class_(
    "PbHubPWMOutput", output.FloatOutput, cg.Component
)
PbHubServoOutput = pbhub_ns.class_(
    "PbHubServoOutput", output.FloatOutput, cg.Component
)

CONF_ZERO_MEANS_ZERO = output.CONF_ZERO_MEANS_ZERO


def _prepare_output_config(config):
    if CONF_MODE not in config:
        raise cv.Invalid(
            "PBHUB output mode is required; choose 'pwm' or 'servo'",
            path=[CONF_MODE],
        )
    config = config.copy()
    if (
        config.get(CONF_MODE) == OUTPUT_MODE_SERVO
        and CONF_ZERO_MEANS_ZERO not in config
    ):
        config[CONF_ZERO_MEANS_ZERO] = True
    return config


def _validate_servo_transforms(config):
    if config[CONF_MODE] != OUTPUT_MODE_SERVO:
        return config
    if config.get(CONF_INVERTED, False):
        raise cv.Invalid(
            "PBHUB servo mode does not support output inversion",
            path=[CONF_INVERTED],
        )
    if config.get(CONF_MIN_POWER, 0.0) != 0.0:
        raise cv.Invalid(
            "PBHUB servo mode requires min_power: 0%",
            path=[CONF_MIN_POWER],
        )
    if config.get(CONF_MAX_POWER, 1.0) != 1.0:
        raise cv.Invalid(
            "PBHUB servo mode requires max_power: 100%",
            path=[CONF_MAX_POWER],
        )
    if not config[CONF_ZERO_MEANS_ZERO]:
        raise cv.Invalid(
            "PBHUB servo mode requires zero_means_zero: true so zero detaches",
            path=[CONF_ZERO_MEANS_ZERO],
        )
    return config


_COMMON_SCHEMA = {
    cv.Required(CONF_PBHUB_ID): cv.use_id(PbHubComponent),
    cv.Required(CONF_PIN): validate_endpoint,
}

CONFIG_SCHEMA = cv.All(
    _prepare_output_config,
    cv.typed_schema(
        {
            OUTPUT_MODE_PWM: output.FLOAT_OUTPUT_SCHEMA.extend(
                {
                    cv.Required(CONF_ID): cv.declare_id(PbHubPWMOutput),
                    **_COMMON_SCHEMA,
                }
            ),
            OUTPUT_MODE_SERVO: output.FLOAT_OUTPUT_SCHEMA.extend(
                {
                    cv.Required(CONF_ID): cv.declare_id(PbHubServoOutput),
                    **_COMMON_SCHEMA,
                }
            ),
        },
        key=CONF_MODE,
    ),
    _validate_servo_transforms,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent, config[CONF_PIN])
    owner = (
        EndpointOwner.SERVO
        if config[CONF_MODE] == OUTPUT_MODE_SERVO
        else EndpointOwner.PWM
    )
    cg.add(
        parent.claim_endpoint(config[CONF_PIN], owner, str(config[CONF_ID]))
    )
    cg.add(parent.register_recovery_client(var))
    await output.register_output(var, config)
    await cg.register_component(var, config)
