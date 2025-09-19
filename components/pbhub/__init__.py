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

# Public key and classes
pbhub_ns = cg.esphome_ns.namespace("pbhub")
PbHubComponent = pbhub_ns.class_("PbHubComponent", cg.Component, i2c.I2CDevice)
PbHubGPIOPin   = pbhub_ns.class_("PbHubGPIOPin", cg.GPIOPin)

CONF_PBHUB = "pbhub"

# ---- Base component: i2c::I2CDevice (mux-ready) ----------------------
CONFIG_SCHEMA = cv.All(
    cv.ensure_list(
        cv.Schema({
            cv.Required(CONF_ID): cv.declare_id(PbHubComponent),
        }).extend(cv.COMPONENT_SCHEMA).extend(
            i2c.i2c_device_schema(0x61)
        )
    )
)

async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        await i2c.register_i2c_device(var, conf)

# ---- Pin Provider: use in pin: { pbhub: <id>, number: <nn> } ---------
def _validate_mode(value):
    if not (value.get(CONF_INPUT, False) or value.get(CONF_OUTPUT, False)):
        raise cv.Invalid("Mode must be either input or output")
    if value.get(CONF_INPUT, False) and value.get(CONF_OUTPUT, False):
        raise cv.Invalid("Mode must be either input or output (not both)")
    return value

PBHUB_PIN_SCHEMA = cv.All({
    cv.GenerateID(): cv.declare_id(PbHubGPIOPin),
    cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
    cv.Required(CONF_NUMBER): cv.int_range(min=0, max=51),
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
