import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import i2c
from esphome.const import (
    CONF_ID,
    CONF_INPUT,
    CONF_INVERTED,
    CONF_MODE,
    CONF_NUM_LEDS,
    CONF_NUMBER,
    CONF_OUTPUT,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

CONF_PBHUB = "pbhub"
CONF_PBHUB_ID = "pbhub_id"
CONF_SLOT = "slot"

OUTPUT_MODE_PWM = "pwm"
OUTPUT_MODE_SERVO = "servo"

VALID_ENDPOINTS = (0, 1, 10, 11, 20, 21, 30, 31, 40, 41, 50, 51)
VALID_ENDPOINTS_TEXT = ", ".join(str(endpoint) for endpoint in VALID_ENDPOINTS)

pbhub_ns = cg.esphome_ns.namespace("pbhub")
PbHubComponent = pbhub_ns.class_("PbHubComponent", cg.Component, i2c.I2CDevice)
PbHubGPIOPin = pbhub_ns.class_("PbHubGPIOPin", cg.GPIOPin)


def validate_slot(value):
    value = cv.int_(value)
    if not 0 <= value <= 5:
        raise cv.Invalid("PBHUB slot must be an integer from 0 to 5")
    return value


def validate_endpoint(value):
    value = cv.int_(value)
    slot, index = divmod(value, 10)
    if not 0 <= slot <= 5 or index not in (0, 1):
        raise cv.Invalid(
            "PBHUB pin must use slot * 10 + signal, where slot is 0..5 and "
            f"signal is 0 or 1; accepted values: {VALID_ENDPOINTS_TEXT}"
        )
    return value


def validate_led_count(value):
    value = cv.int_(value)
    if not 1 <= value <= 74:
        raise cv.Invalid("PBHUB num_leds must be an integer from 1 to 74")
    return value


def validate_led_timing_mode(value):
    value = cv.int_(value)
    if value not in (0, 1):
        raise cv.Invalid("PBHUB LED timing mode must be 0 or 1")
    return value


def validate_output_mode(value):
    value = cv.string_strict(value)
    if value == OUTPUT_MODE_SERVO:
        raise cv.Invalid(
            "PBHUB output mode 'servo' is reserved for Phase 5 and is not "
            "implemented yet"
        )
    if value != OUTPUT_MODE_PWM:
        raise cv.Invalid("PBHUB output mode must be 'pwm'")
    return value


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PbHubComponent),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x61))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)


def _validate_gpio_mode(value):
    value = value or {}
    if value.get(CONF_INPUT, False) and value.get(CONF_OUTPUT, False):
        raise cv.Invalid("Only one of 'input' or 'output' can be true")
    return value


# Temporary legacy binding retained only until the native digital entities land
# in Phase 3. Do not use this schema for new v2 examples.
PBHUB_PIN_SCHEMA = cv.All(
    {
        cv.GenerateID(): cv.declare_id(PbHubGPIOPin),
        cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
        cv.Required(CONF_NUMBER): validate_endpoint,
        cv.Optional(CONF_MODE, default={}): cv.All(
            {
                cv.Optional(CONF_INPUT, default=False): cv.boolean,
                cv.Optional(CONF_OUTPUT, default=False): cv.boolean,
            },
            _validate_gpio_mode,
        ),
        cv.Optional(CONF_INVERTED, default=False): cv.boolean,
    }
)


@pins.PIN_SCHEMA_REGISTRY.register("pbhub", PBHUB_PIN_SCHEMA)
async def pbhub_pin_to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    parent = await cg.get_variable(config[CONF_PBHUB])
    cg.add(var.set_parent(parent))
    cg.add(var.set_pin(config[CONF_NUMBER]))
    cg.add(var.set_inverted(config[CONF_INVERTED]))
    cg.add(var.set_flags(pins.gpio_flags_expr(config[CONF_MODE])))
    return var
