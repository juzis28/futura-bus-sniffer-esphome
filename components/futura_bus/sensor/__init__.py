import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import STATE_CLASS_MEASUREMENT

from .. import (
    futura_bus_ns,
    FuturaBusComponent,
    CONF_FUTURA_BUS_ID,
    CONF_SLAVE_ID,
)

DEPENDENCIES = ["futura_bus"]

CONF_SENSOR_TYPE = "sensor_type"

# Damper sensors
SENSOR_TYPE_POSITION = "position"      # reg 102 — target position %
SENSOR_TYPE_STATUS   = "status"        # reg 107 — status code

# Zone device sensors
SENSOR_TYPE_ZONE_TEMP     = "zone_temp"
SENSOR_TYPE_ZONE_HUMIDITY = "zone_humidity"
SENSOR_TYPE_ZONE_CO2      = "zone_co2"

ALL_SENSOR_TYPES = [
    SENSOR_TYPE_POSITION,
    SENSOR_TYPE_STATUS,
    SENSOR_TYPE_ZONE_TEMP,
    SENSOR_TYPE_ZONE_HUMIDITY,
    SENSOR_TYPE_ZONE_CO2,
]

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            cv.GenerateID(CONF_FUTURA_BUS_ID): cv.use_id(FuturaBusComponent),
            cv.Required(CONF_SLAVE_ID):        cv.int_range(min=1, max=247),
            cv.Required(CONF_SENSOR_TYPE):     cv.one_of(*ALL_SENSOR_TYPES, lower=True),
        }
    )
)


async def to_code(config):
    parent   = await cg.get_variable(config[CONF_FUTURA_BUS_ID])
    var      = await sensor.new_sensor(config)
    slave_id = config[CONF_SLAVE_ID]
    stype    = config[CONF_SENSOR_TYPE]

    if stype == SENSOR_TYPE_POSITION:
        cg.add(parent.register_position_sensor(slave_id, var))
    elif stype == SENSOR_TYPE_STATUS:
        cg.add(parent.register_status_sensor(slave_id, var))
    elif stype == SENSOR_TYPE_ZONE_TEMP:
        cg.add(parent.register_zone_temp(slave_id, var))
    elif stype == SENSOR_TYPE_ZONE_HUMIDITY:
        cg.add(parent.register_zone_humidity(slave_id, var))
    elif stype == SENSOR_TYPE_ZONE_CO2:
        cg.add(parent.register_zone_co2(slave_id, var))
