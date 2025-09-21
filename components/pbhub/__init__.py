import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import i2c
from esphome.const import (
    CONF_ID,
    CONF_ADDRESS,
    CONF_NUMBER,
    CONF_MODE,
    CONF_INVERTED,
    CONF_INPUT,
    CONF_OUTPUT,
)

# Public constant so other sub-platforms can reference it
CONF_PBHUB = "pbhub"

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True  # <-- allow a list of hubs in YAML

pbhub_ns = cg.esphome_ns.namespace("pbhub")
PbHubComponent = pbhub_ns.class_("PbHubComponent", cg.Component, i2c.I2CDevice)
PbHubGPIOPin   = pbhub_ns.class_("PbHubGPIOPin", cg.GPIOPin)

# ---------- Core component schema ----------
COMPONENT_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_ID): cv.declare_id(PbHubComponent),
    cv.Optional(CONF_ADDRESS, default=0x61): cv.i2c_address,
}).extend(i2c.i2c_device_schema(default_address=0x61))

CONFIG_SCHEMA = COMPONENT_SCHEMA  # single-item schema; MULTI_CONF makes it a list

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

# ---------- GPIO pin binding ----------
def _validate_mode(value):
    value = value or {}
    # Disallow input+output simultaneously
    if value.get(CONF_INPUT, False) and value.get(CONF_OUTPUT, False):
        raise cv.Invalid("Only one of 'input' or 'output' can be true")
    return value

PBHUB_PIN_SCHEMA = cv.All({
    cv.GenerateID(): cv.declare_id(PbHubGPIOPin),
    cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
    cv.Required(CONF_NUMBER): cv.int_range(min=0, max=51),  # slot*10+idx (00..51)
    cv.Optional(CONF_MODE, default={}): cv.All({
        cv.Optional(CONF_INPUT, default=False): cv.boolean,
        cv.Optional(CONF_OUTPUT, default=False): cv.boolean,
    }, _validate_mode),
    cv.Optional(CONF_INVERTED, default=False): cv.boolean,
})

@pins.PIN_SCHEMA_REGISTRY.register("pbhub", PBHUB_PIN_SCHEMA)
async def pbhub_pin_to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    parent = await cg.get_variable(config[CONF_PBHUB])
    cg.add(var.set_parent(parent))
    cg.add(var.set_pin(config[CONF_NUMBER]))
    cg.add(var.set_inverted(config[CONF_INVERTED]))
    cg.add(var.set_flags(pins.gpio_flags_expr(config[CONF_MODE])))
    return var