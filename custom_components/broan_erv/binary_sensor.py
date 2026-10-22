"""Reported operation and selected diagnostic binary sensors."""

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity

from .const import CONF_SOURCES, SOURCES, operating_value
from .entity import BroanEntity


async def async_setup_entry(hass, entry, async_add_entities):
    sources = entry.data[CONF_SOURCES]
    entities = [BroanOperating(entry, sources.get("operating", sources["active_mode"]), "operating" in sources)]
    entities.extend(
        BroanBinarySensor(entry, definition, entry.data[CONF_SOURCES][definition.key])
        for definition in SOURCES
        if definition.domain == "binary_sensor" and definition.key != "operating" and definition.key in sources
    )
    async_add_entities(entities)


class BroanOperating(BroanEntity, BinarySensorEntity):
    _attr_device_class = BinarySensorDeviceClass.RUNNING
    _attr_icon = "mdi:fan"

    def __init__(self, entry, source_id, native=False):
        super().__init__(entry, "operating", "Operating", source_id)
        self._native = native

    @property
    def is_on(self):
        state = self.source_state
        if self._native:
            return state.state == "on" if state and state.state in ("on", "off") else None
        return operating_value(state.state) if state is not None else None


class BroanBinarySensor(BroanEntity, BinarySensorEntity):
    def __init__(self, entry, definition, source_id):
        super().__init__(entry, definition.key, definition.name, source_id, definition.diagnostic)
        self._attr_device_class = definition.device_class

    @property
    def is_on(self):
        state = self.source_state
        return state.state == "on" if state and state.state in ("on", "off") else None
