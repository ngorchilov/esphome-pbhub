import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import i2c
from esphome.const import (
    CONF_ID,
    CONF_MODE,
    CONF_NUM_LEDS,
    CONF_OUTPUT_ID,
    CONF_PIN,
    CONF_PLATFORM,
)

CODEOWNERS = ["@ngorchilov"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

CONF_PBHUB_ID = "pbhub_id"
CONF_SLOT = "slot"

OUTPUT_MODE_PWM = "pwm"
OUTPUT_MODE_SERVO = "servo"

VALID_ENDPOINTS = (0, 1, 10, 11, 20, 21, 30, 31, 40, 41, 50, 51)
VALID_ENDPOINTS_TEXT = ", ".join(str(endpoint) for endpoint in VALID_ENDPOINTS)

pbhub_ns = cg.esphome_ns.namespace("pbhub")
PbHubComponent = pbhub_ns.class_("PbHubComponent", cg.Component, i2c.I2CDevice)
EndpointOwner = pbhub_ns.enum("EndpointOwner", is_class=True)


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


def _path_text(path):
    text = ""
    for part in path:
        if isinstance(part, int):
            text += f"[{part}]"
        else:
            text += ("." if text else "") + str(part)
    return text


def _collect_endpoint_claims(full_config):
    specs = (
        (
            "output",
            CONF_PIN,
            lambda entry: entry[CONF_PIN],
            lambda entry: (
                "servo output"
                if entry.get(CONF_MODE) == OUTPUT_MODE_SERVO
                else "PWM output"
            ),
        ),
        (
            "sensor",
            CONF_SLOT,
            lambda entry: entry[CONF_SLOT] * 10,
            lambda _entry: "ADC sensor",
        ),
        (
            "light",
            CONF_SLOT,
            lambda entry: entry[CONF_SLOT] * 10 + 1,
            lambda _entry: "RGB light",
        ),
        (
            "binary_sensor",
            CONF_PIN,
            lambda entry: entry[CONF_PIN],
            lambda _entry: "digital input",
        ),
        (
            "switch",
            CONF_PIN,
            lambda entry: entry[CONF_PIN],
            lambda _entry: "digital output",
        ),
    )
    claims = []
    for rank, (domain, field, endpoint_fn, label_fn) in enumerate(specs):
        for index, entry in enumerate(full_config.get(domain, [])):
            if entry.get(CONF_PLATFORM) != "pbhub":
                continue
            entity_id = entry.get(CONF_ID, entry.get(CONF_OUTPUT_ID, "<generated>"))
            path = [domain, index]
            claims.append(
                {
                    "hub": str(entry[CONF_PBHUB_ID]),
                    "endpoint": endpoint_fn(entry),
                    "rank": rank,
                    "index": index,
                    "path": path,
                    "path_text": _path_text(path),
                    "field": field,
                    "label": label_fn(entry),
                    "entity_id": str(entity_id),
                }
            )
    return sorted(
        claims,
        key=lambda claim: (
            claim["hub"],
            claim["endpoint"],
            claim["rank"],
            claim["index"],
        ),
    )


def _final_validate(config):
    full_config = fv.full_config.get()
    data_key = "pbhub.endpoint_claims_validated"
    if full_config.data.get(data_key):
        return config
    full_config.data[data_key] = True

    claimed = {}
    for claim in _collect_endpoint_claims(full_config):
        key = (claim["hub"], claim["endpoint"])
        existing = claimed.get(key)
        if existing is None:
            claimed[key] = claim
            continue

        raise cv.FinalExternalInvalid(
            f"PBHUB endpoint {claim['endpoint']} on hub '{claim['hub']}' is "
            f"claimed by both {existing['label']} '{existing['entity_id']}' at "
            f"{existing['path_text']} and {claim['label']} "
            f"'{claim['entity_id']}' at {claim['path_text']}",
            path=[cv.ROOT_CONFIG_PATH, *claim["path"], claim["field"]],
        )

    return config


FINAL_VALIDATE_SCHEMA = _final_validate
