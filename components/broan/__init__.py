from __future__ import annotations

from esphome import pins
import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_FLOW_CONTROL_PIN
from esphome.cpp_helpers import gpio_pin_expression

AUTO_LOAD = []
DEPENDENCIES = ["uart"]
CODEOWNERS = ["@nspitko"]

broan_ns = cg.esphome_ns.namespace("broan")
BroanComponent = broan_ns.class_("BroanComponent", cg.Component, uart.UARTDevice)

CONF_BROAN_ID = "broan_id"

def validate_timeout(value):
    period = cv.positive_time_period_milliseconds(value)
    if not 1 <= period.total_milliseconds <= 0x7FFFFFFF:
        raise cv.Invalid("timeout must be between 1ms and 2147483647ms")
    return period

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BroanComponent),
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            cv.Optional("passive_mode", default=False): cv.boolean,
            cv.Optional("experimental_query", default=False): cv.boolean,
            cv.Optional("query_source_address", default=0x13): cv.All(
                cv.hex_uint8_t, cv.Range(min=0x13, max=0x1F)),
            cv.Optional("communication_timeout", default="120s"): validate_timeout,
            cv.Optional("sensor_timeout", default="300s"): validate_timeout,
            cv.Optional("log_packets", default=False): cv.boolean,
            cv.Optional("log_unknown_registers", default=False): cv.boolean,
            cv.Optional("register_discovery", default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

BroanBaseSchema = cv.Schema(
    {
        cv.GenerateID(CONF_BROAN_ID): cv.use_id(BroanComponent),
    },
)

def FINAL_VALIDATE_SCHEMA(config):
    if config["experimental_query"]:
        if config["passive_mode"]:
            raise cv.Invalid("experimental_query cannot enable transmissions in passive_mode")
        if CONF_FLOW_CONTROL_PIN not in config:
            raise cv.Invalid("experimental_query requires a transmit flow_control_pin")
        def manual_direction(hub):
            if "flow_control_pin" in hub:
                raise cv.Invalid("experimental_query uses broan.flow_control_pin, not UART flow control")
            return hub
        cv.Schema({cv.Required("uart_id"): fv.id_declaration_match_schema(manual_direction)},
                  extra=cv.ALLOW_EXTRA)(config)
    if config["passive_mode"] and CONF_FLOW_CONTROL_PIN in config:
        raise cv.Invalid("passive_mode must not configure a transmit flow_control_pin")
    if config["passive_mode"]:
        def receive_only(hub):
            if "tx_pin" in hub or "flow_control_pin" in hub:
                raise cv.Invalid("passive_mode requires an RX-only UART without tx_pin or flow_control_pin")
            return hub
        cv.Schema({cv.Required("uart_id"): fv.id_declaration_match_schema(receive_only)},
                  extra=cv.ALLOW_EXTRA)(config)
    return uart.final_validate_device_schema(
        "broan", require_tx=not config["passive_mode"], require_rx=True,
        baud_rate=38400, data_bits=8, parity="NONE", stop_bits=1,
    )(config)

async def to_code(config):
    cg.add_global(broan_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    await uart.register_uart_device(var, config)
    for option in ("passive_mode", "communication_timeout", "sensor_timeout",
                   "log_packets", "log_unknown_registers", "register_discovery",
                   "experimental_query", "query_source_address"):
        cg.add(getattr(var, f"set_{option}")(config[option]))

    if CONF_FLOW_CONTROL_PIN in config:
        pin = await gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))
