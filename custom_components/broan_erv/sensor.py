"""Expose selected ERV telemetry on the Broan integration's device."""

from homeassistant.components.sensor import SensorEntity, SensorStateClass

from .const import CONF_SOURCES, SOURCES, mode_label, numeric_value
from .entity import BroanEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([
        BroanSensor(entry, definition, entry.data[CONF_SOURCES][definition.key])
        for definition in SOURCES
        if definition.domain == "sensor" and definition.key in entry.data[CONF_SOURCES]
    ])


class BroanSensor(BroanEntity, SensorEntity):
    def __init__(self, entry, definition, source_id):
        super().__init__(entry, definition.key, definition.name, source_id, definition.diagnostic)
        self._definition = definition
        self._last_unit = None
        self._attr_device_class = definition.device_class
        if definition.measurement:
            self._attr_state_class = SensorStateClass.MEASUREMENT

    @property
    def native_unit_of_measurement(self):
        state = self.source_state
        if self._definition.numeric and state is not None:
            self._last_unit = state.attributes.get("unit_of_measurement", self._last_unit)
        return self._last_unit

    @property
    def native_value(self):
        state = self.source_state
        if not self.available or state is None:
            return None
        if self._definition.numeric:
            return numeric_value(state.state)
        if self._definition.key in ("active_mode", "base_mode"):
            return mode_label(self._definition.key, state.state)
        return state.state

    @property
    def available(self):
        if not super().available:
            return False
        return not self._definition.numeric or numeric_value(self.source_state.state) is not None
