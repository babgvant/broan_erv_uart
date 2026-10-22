"""Broan ERV source definitions and state decoding."""

from dataclasses import dataclass
import math

DOMAIN = "broan_erv"
CONF_DEVICE = "device_id"
CONF_SOURCES = "sources"


@dataclass(frozen=True)
class SourceDefinition:
    key: str
    name: str
    domain: str = "sensor"
    numeric: bool = False
    device_class: str | None = None
    measurement: bool = False
    diagnostic: bool = False
    aliases: tuple[str, ...] = ()


SOURCES = (
    SourceDefinition("active_mode", "Active mode"),
    SourceDefinition("base_mode", "Base mode"),
    SourceDefinition("power", "Power", numeric=True, device_class="power", measurement=True),
    SourceDefinition("temperature", "Intake temperature", numeric=True, device_class="temperature", measurement=True),
    SourceDefinition("temperature_out", "Exhaust temperature", numeric=True, device_class="temperature", measurement=True),
    SourceDefinition("supply_fan_rpm", "Supply fan RPM", numeric=True, measurement=True),
    SourceDefinition("exhaust_fan_rpm", "Exhaust fan RPM", numeric=True, measurement=True),
    SourceDefinition("supply_fan_cfm", "Supply fan CFM", numeric=True, measurement=True),
    SourceDefinition("exhaust_fan_cfm", "Exhaust fan CFM", numeric=True, measurement=True),
    SourceDefinition("filter_life", "Filter life", numeric=True, device_class="duration"),
    SourceDefinition("humidity_target", "Humidity target", numeric=True, device_class="humidity", diagnostic=True),
    SourceDefinition("intermittent_on_time", "Intermittent on time", numeric=True, device_class="duration", diagnostic=True),
    SourceDefinition("bath_boost_remaining", "Bath boost remaining", numeric=True, device_class="duration"),
    SourceDefinition("turbo_remaining", "Turbo remaining", numeric=True, device_class="duration"),
    SourceDefinition("fault_code", "Fault code", diagnostic=True),
    SourceDefinition("warning_code", "Warning code", diagnostic=True),
    SourceDefinition("last_fault", "Last fault", diagnostic=True),
    SourceDefinition("model", "Model", diagnostic=True),
    SourceDefinition("firmware", "Firmware", diagnostic=True),
    SourceDefinition("firmware_version", "Firmware version", diagnostic=True),
    SourceDefinition("hardware_revision", "Hardware revision", diagnostic=True, aliases=("Hardware Rev",)),
    SourceDefinition("operating", "Operating", domain="binary_sensor", device_class="running"),
    SourceDefinition("fault_active", "Fault active", domain="binary_sensor", device_class="problem", diagnostic=True),
    SourceDefinition("fault_data_available", "Fault data available", domain="binary_sensor", diagnostic=True),
    SourceDefinition("communication_ok", "RS485 communication", domain="binary_sensor", device_class="connectivity", diagnostic=True),
    SourceDefinition("humidity_control_enabled", "Humidity control enabled", domain="binary_sensor", diagnostic=True),
)
SOURCE_BY_KEY = {source.key: source for source in SOURCES}
ACTIVE_MODES = {0: "Idle", 1: "Low", 2: "High", 3: "Turbo", 4: "Medium", 5: "Bath Override"}
BASE_MODES = {1: "Standby", 8: "Intermittent", 9: "Continuous Low", 10: "Continuous High", 11: "Continuous Medium", 17: "Smart"}


def known_value(value: str | None) -> bool:
    return value is not None and value.strip().lower() not in ("", "unknown", "unavailable", "none", "nan")


def numeric_value(value: str | None) -> float | None:
    if not known_value(value):
        return None
    try:
        number = float(value)
    except (ValueError, TypeError):
        return None
    return number if math.isfinite(number) else None


def mode_code(value: str | None) -> int | None:
    number = numeric_value(value)
    return int(number) if number is not None and number.is_integer() else None


def operating_value(value: str | None) -> bool | None:
    if not known_value(value):
        return None
    for code, label in ACTIVE_MODES.items():
        if value.strip().lower() == label.lower():
            return code != 0
    code = mode_code(value)
    return code != 0 if code in ACTIVE_MODES else None


def mode_label(key: str, value: str) -> str:
    modes = ACTIVE_MODES if key == "active_mode" else BASE_MODES
    return modes.get(mode_code(value), value)


def matches_source(definition: SourceDefinition, name: str) -> bool:
    normalized = name.lower().replace("_", " ").strip()
    labels = (definition.key.replace("_", " "), definition.name.lower(), *(alias.lower() for alias in definition.aliases))
    return any(normalized == label or normalized.endswith(f" {label}") for label in labels)
