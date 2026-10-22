"""Entities backed by registry-stable ESPHome source references."""

from homeassistant.core import callback
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.entity import DeviceInfo, Entity, EntityCategory
from homeassistant.helpers.event import async_track_state_change_event

from .const import CONF_DEVICE, DOMAIN, known_value


class BroanEntity(Entity):
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, entry, key, name, source_id, diagnostic=False):
        self._source_id = source_id
        self._source_entity_id = None
        self._attr_unique_id = f"{entry.entry_id}_{key}"
        self._attr_name = name
        if diagnostic:
            self._attr_entity_category = EntityCategory.DIAGNOSTIC
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.data[CONF_DEVICE])},
            name=entry.title,
            manufacturer="Broan / NuTone / Venmar / vanEE",
            model="ERV UART (ESPHome)",
        )

    @property
    def source_state(self):
        if self._source_entity_id is None:
            return None
        return self.hass.states.get(self._source_entity_id)

    @property
    def available(self):
        state = self.source_state
        return state is not None and known_value(state.state)

    @property
    def extra_state_attributes(self):
        return {"source_entity": self._source_entity_id}

    async def async_added_to_hass(self):
        await super().async_added_to_hass()
        registry = er.async_get(self.hass)
        if (source := registry.async_get(self._source_id)) is not None:
            self._source_entity_id = source.entity_id
            self.async_on_remove(async_track_state_change_event(
                self.hass, [source.entity_id], self._source_changed,
            ))

    @callback
    def _source_changed(self, event):
        self.async_write_ha_state()
