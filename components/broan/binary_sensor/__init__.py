import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC
from .. import BroanComponent, CONF_BROAN_ID

DEPENDENCIES = ["broan"]
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),
    cv.Optional("fault_active"): binary_sensor.binary_sensor_schema(
        device_class="problem", entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    cv.Optional("communication_ok"): binary_sensor.binary_sensor_schema(
        device_class="connectivity", entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    cv.Optional("fault_data_available"): binary_sensor.binary_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    cv.Optional("humidity_control_enabled"): binary_sensor.binary_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_BROAN_ID])
    for key in ("fault_active", "communication_ok", "fault_data_available", "humidity_control_enabled"):
        if key in config:
            entity = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(parent, f"set_{key}_binary_sensor")(entity))
