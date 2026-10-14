from pathlib import Path
import re
from types import SimpleNamespace

from jinja2 import Environment
import pytest
import yaml

PACKAGE = yaml.safe_load((Path(__file__).resolve().parents[1] /
                         "examples/home-assistant-package.yaml").read_text())

def environment(states):
    env = Environment()
    env.globals.update(
        states=lambda entity: states.get(entity, "unknown"),
        is_state=lambda entity, expected: states.get(entity) == expected,
        has_value=lambda entity: states.get(entity, "unknown") not in ("unknown", "unavailable"),
    )
    env.tests["match"] = lambda value, pattern: re.match(pattern, value) is not None
    return env

def test_all_templates_parse():
    env = environment({})
    def check(value):
        if isinstance(value, dict):
            for item in value.values(): check(item)
        elif isinstance(value, list):
            for item in value: check(item)
        elif isinstance(value, str) and "{{" in value:
            env.parse(value)
    check(PACKAGE)

@pytest.mark.parametrize("changes,expected", [
    ({}, True),
    ({"sensor.adu_erv_exhaust_fan_rpm": "unknown"}, False),
    ({"sensor.adu_erv_exhaust_fan_rpm": "unavailable"}, False),
    ({"sensor.adu_erv_exhaust_fan_rpm": "1250"}, False),
    ({"sensor.adu_erv_exhaust_fan_rpm": "-1"}, False),
    ({"sensor.adu_erv_active_mode_code": "0"}, False),
    ({"sensor.adu_erv_base_mode_code": "8"}, False),
    ({"sensor.adu_erv_base_mode_code": "6"}, False),
    ({"sensor.adu_erv_active_mode_code": "unknown"}, False),
    ({"input_boolean.adu_erv_fan_alerts_enabled": "off"}, False),
    ({"binary_sensor.adu_erv_rs485_communication": "off"}, False),
])
def test_stopped_fan_alert(changes, expected):
    values = {
        "sensor.adu_erv_exhaust_fan_rpm": "0",
        "sensor.adu_erv_active_mode_code": "1.0",
        "sensor.adu_erv_base_mode_code": "9.0",
        "input_boolean.adu_erv_fan_alerts_enabled": "on",
        "binary_sensor.adu_erv_rs485_communication": "on",
    }
    values.update(changes)
    automation = next(item for item in PACKAGE["automation"] if item["id"] == "adu_erv_exhaust_stopped")
    rendered = environment(values).from_string(automation["triggers"][0]["value_template"]).render().strip()
    assert (rendered == "True") == expected

@pytest.mark.parametrize("state,available", [("1250", True), ("0", True), ("unknown", False), ("unavailable", False)])
def test_rpm_availability(state, available):
    sensor = PACKAGE["template"][0]["sensor"][0]
    rendered = environment({"sensor.adu_erv_exhaust_fan_rpm": state}).from_string(sensor["availability"]).render()
    assert (rendered == "True") == available

@pytest.mark.parametrize("new_state,retained,record", [
    ("E38 at boot + 10s", "unknown", True),
    ("E38 at boot + 10s", "E38 at boot + 10s", False),
    ("E38 at boot + 20s", "E38 at boot + 10s", True),
    ("unavailable", "E38 at boot + 10s", False),
])
def test_retention_timestamp(new_state, retained, record):
    automation = next(item for item in PACKAGE["automation"] if item["id"] == "adu_erv_record_last_fault")
    template = environment({"input_text.adu_erv_last_fault_snapshot": retained}).from_string(
        automation["conditions"][0]["value_template"])
    result = template.render(trigger=SimpleNamespace(to_state=SimpleNamespace(state=new_state))).strip()
    assert (result == "True") == record
