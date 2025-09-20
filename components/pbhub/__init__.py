import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import i2c, output, sensor, light
from esphome.const import (
    CONF_ID,
    CONF_ADDRESS,
    CONF_NUMBER,
    CONF_MODE,
    CONF_INVERTED,
    CONF_INPUT,
    CONF_OUTPUT,
)

CODEOWNERS = ["@ngorchilov"]

# -----------------------------------------------------------------------------
# Namespace
# -----------------------------------------------------------------------------
pbhub_ns = cg.esphome_ns.namespace("pbhub")

# -----------------------------------------------------------------------------
# Core Component
# -----------------------------------------------------------------------------
PbHubComponent = pbhub_ns.class_("PbHubComponent", cg.Component, i2c.I2CDevice)
CONF_PBHUB = "pbhub"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PbHubComponent),
        cv.Optional(CONF_ADDRESS): cv.i2c_address,
    }
).extend(i2c.i2c_device_schema(CONF_ADDRESS))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

# -----------------------------------------------------------------------------
# GPIO Pin binding
# -----------------------------------------------------------------------------
PbHubGPIOPin = pbhub_ns.class_("PbHubGPIOPin", cg.GPIOPin)

PBHUB_PIN_SCHEMA = cv.All(
    {
        cv.GenerateID(): cv.declare_id(PbHubGPIOPin),
        cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
        cv.Required(CONF_NUMBER): cv.int_range(min=0, max=51),
        cv.Optional(CONF_MODE, default={}): cv.All(
            {
                cv.Optional(CONF_INPUT, default=False): cv.boolean,
                cv.Optional(CONF_OUTPUT, default=False): cv.boolean,
            },
            pins.gpio_validate_modes,  # reuse ESPHome's gpio validation
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

# -----------------------------------------------------------------------------
# ADC Sensor
# -----------------------------------------------------------------------------
PbHubADC = pbhub_ns.class_("PbHubADC", sensor.Sensor, cg.PollingComponent)

SENSOR_SCHEMA = sensor.sensor_schema(PbHubADC).extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(PbHubADC),
        cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
        cv.Required(CONF_NUMBER): cv.int_range(min=0, max=5),  # slot index
    }
).extend(cv.polling_component_schema("1s"))

async def pbhub_adc_to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB])
    var = cg.new_Pvariable(
        config[CONF_ID], parent, config[CONF_NUMBER], config["update_interval"]
    )
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)


# -----------------------------------------------------------------------------
# PWM Output
# -----------------------------------------------------------------------------
PbHubPWMPin = pbhub_ns.class_("PbHubPWMPin", output.FloatOutput)

PWM_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(PbHubPWMPin),
        cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
        cv.Required(CONF_NUMBER): cv.int_range(min=0, max=51),
    }
)

async def pbhub_pwm_to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB])
    var = cg.new_Pvariable(config[CONF_ID], parent, config[CONF_NUMBER])
    await output.register_output(var, config)


# -----------------------------------------------------------------------------
# RGB Light
# -----------------------------------------------------------------------------
PbHubRGBLight = pbhub_ns.class_("PbHubRGBLight", light.LightOutput)

LIGHT_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(PbHubRGBLight),
        cv.Required(CONF_PBHUB): cv.use_id(PbHubComponent),
        cv.Required(CONF_NUMBER): cv.int_range(min=0, max=5),  # slot index
        cv.Optional("led_count", default=1): cv.int_range(min=1, max=1024),
    }
)

async def pbhub_rgb_to_code(config):
    parent = await cg.get_variable(config[CONF_PBHUB])
    var = cg.new_Pvariable(config[CONF_ID], parent, config[CONF_NUMBER])
    cg.add(var.set_led_count(config["led_count"]))
    await light.register_light(var, config)
