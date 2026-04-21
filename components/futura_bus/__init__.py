import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UART_ID
from esphome.components import uart
from esphome.components import sensor as esph_sensor
from esphome.components import text_sensor as esph_text_sensor

CODEOWNERS = ["@juzis28"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor"]
MULTI_CONF = False

# ── Config keys ───────────────────────────────────────────────────────────────
CONF_FUTURA_BUS_ID       = "futura_bus_id"
CONF_DAMPERS             = "dampers"
CONF_ZONE_DEVICES        = "zone_devices"
CONF_PRESSURE_SENSORS    = "pressure_sensors"
CONF_SLAVE_ID            = "slave_id"
CONF_REG_ADDR            = "reg_addr"
CONF_LABEL               = "label"
CONF_ROOM                = "room"
CONF_FRAME_GAP_MS        = "frame_gap_ms"
CONF_DISCOVERY_MODE      = "discovery_mode"
CONF_SUMMARY_INTERVAL_S  = "summary_interval_s"
CONF_FRAMES_SENSOR       = "frames_sensor"
CONF_VALID_SENSOR        = "valid_frames_sensor"
CONF_BYTES_SENSOR        = "bytes_sensor"
CONF_DISCOVERY_SENSOR    = "discovery_sensor"
CONF_FAN_AIRFLOW_SENSOR  = "fan_airflow_sensor"
CONF_EXHAUST_FAN_SENSOR  = "exhaust_fan_sensor"
CONF_SUPPLY_FAN_SENSOR   = "supply_fan_sensor"

# ── C++ binding ───────────────────────────────────────────────────────────────
futura_bus_ns = cg.esphome_ns.namespace("futura_bus")
FuturaBusComponent = futura_bus_ns.class_(
    "FuturaBusComponent", cg.Component, uart.UARTDevice
)

# ── Sub-schemas ───────────────────────────────────────────────────────────────
DAMPER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SLAVE_ID):    cv.int_range(min=1, max=247),
        cv.Required(CONF_ROOM):        cv.string,
    }
)

ZONE_DEVICE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SLAVE_ID): cv.int_range(min=1, max=247),
        cv.Required(CONF_ROOM):     cv.string,
    }
)

# Pressure sensor schema:
#   slave_id + reg_addr pinpoint exactly which Modbus register carries the pressure.
#   label    = human-readable name shown in HA.
#   sensor   = inline sensor definition (name, unit etc.)
PRESSURE_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SLAVE_ID): cv.int_range(min=1, max=247),
        cv.Required(CONF_REG_ADDR): cv.int_range(min=0, max=65535),
        cv.Required(CONF_LABEL):    cv.string,
        cv.Required("sensor"):      esph_sensor.sensor_schema(
            unit_of_measurement="",
            accuracy_decimals=0,
        ),
    }
)

# ── Main schema ───────────────────────────────────────────────────────────────
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FuturaBusComponent),

            cv.Optional(CONF_FRAME_GAP_MS,      default=3.0):  cv.float_range(min=1.0, max=100.0),
            cv.Optional(CONF_DISCOVERY_MODE,    default=True):  cv.boolean,
            cv.Optional(CONF_SUMMARY_INTERVAL_S, default=60):   cv.positive_int,

            cv.Optional(CONF_DAMPERS,          default=[]): cv.ensure_list(DAMPER_SCHEMA),
            cv.Optional(CONF_ZONE_DEVICES,     default=[]): cv.ensure_list(ZONE_DEVICE_SCHEMA),
            cv.Optional(CONF_PRESSURE_SENSORS, default=[]): cv.ensure_list(PRESSURE_SENSOR_SCHEMA),

            # Fan system sensors
            cv.Optional(CONF_FAN_AIRFLOW_SENSOR): esph_sensor.sensor_schema(
                unit_of_measurement="step",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_EXHAUST_FAN_SENSOR): esph_sensor.sensor_schema(
                unit_of_measurement="%",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_SUPPLY_FAN_SENSOR): esph_sensor.sensor_schema(
                unit_of_measurement="%",
                accuracy_decimals=0,
            ),

            # Diagnostic counter sensors
            cv.Optional(CONF_FRAMES_SENSOR): esph_sensor.sensor_schema(accuracy_decimals=0),
            cv.Optional(CONF_VALID_SENSOR):  esph_sensor.sensor_schema(accuracy_decimals=0),
            cv.Optional(CONF_BYTES_SENSOR):  esph_sensor.sensor_schema(accuracy_decimals=0),

            # Discovery text sensor
            cv.Optional(CONF_DISCOVERY_SENSOR): esph_text_sensor.text_sensor_schema(),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_frame_gap_ms(config[CONF_FRAME_GAP_MS]))
    cg.add(var.set_discovery_mode(config[CONF_DISCOVERY_MODE]))
    cg.add(var.set_summary_interval_s(config[CONF_SUMMARY_INTERVAL_S]))

    # Diagnostic numeric sensors
    for key, setter in [
        (CONF_FRAMES_SENSOR, "set_frames_sensor"),
        (CONF_VALID_SENSOR,  "set_valid_sensor"),
        (CONF_BYTES_SENSOR,  "set_bytes_sensor"),
    ]:
        if key in config:
            s = await esph_sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(s))

    # Discovery text sensor
    if CONF_DISCOVERY_SENSOR in config:
        ts = await esph_text_sensor.new_text_sensor(config[CONF_DISCOVERY_SENSOR])
        cg.add(var.set_discovery_sensor(ts))

    # Fan/system sensors
    if CONF_FAN_AIRFLOW_SENSOR in config:
        s = await esph_sensor.new_sensor(config[CONF_FAN_AIRFLOW_SENSOR])
        cg.add(var.register_fan_airflow_sensor(s))
    if CONF_EXHAUST_FAN_SENSOR in config:
        s = await esph_sensor.new_sensor(config[CONF_EXHAUST_FAN_SENSOR])
        cg.add(var.register_exhaust_fan_sensor(s))
    if CONF_SUPPLY_FAN_SENSOR in config:
        s = await esph_sensor.new_sensor(config[CONF_SUPPLY_FAN_SENSOR])
        cg.add(var.register_supply_fan_sensor(s))

    # Damper configs
    for dc in config.get(CONF_DAMPERS, []):
        cg.add(var.add_damper_config(
            dc[CONF_SLAVE_ID], dc[CONF_ROOM],
        ))

    # Zone device configs
    for zd in config.get(CONF_ZONE_DEVICES, []):
        cg.add(var.add_zone_device(zd[CONF_SLAVE_ID], zd[CONF_ROOM]))

    # Pressure sensor configs
    for ps in config.get(CONF_PRESSURE_SENSORS, []):
        sens = await esph_sensor.new_sensor(ps["sensor"])
        cg.add(var.add_pressure_sensor(
            ps[CONF_SLAVE_ID], ps[CONF_REG_ADDR], ps[CONF_LABEL], sens,
        ))
