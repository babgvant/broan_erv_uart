"""Home Assistant companion for the Broan ESPHome UART component."""

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant
from homeassistant.helpers import config_validation as cv, entity_registry as er
from homeassistant.helpers.event import async_track_entity_registry_updated_event

from .const import CONF_SOURCES, DOMAIN

CONFIG_SCHEMA = cv.config_entry_only_config_schema(DOMAIN)
PLATFORMS = (Platform.SENSOR, Platform.BINARY_SENSOR)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    registry = er.async_get(hass)
    source_ids = set(entry.data[CONF_SOURCES].values())
    entity_ids = [entity.entity_id for source_id in source_ids if (entity := registry.async_get(source_id))]

    async def source_updated(event):
        if event.data["action"] == "remove" or "old_entity_id" in event.data:
            await hass.config_entries.async_reload(entry.entry_id)

    entry.async_on_unload(async_track_entity_registry_updated_event(hass, entity_ids, source_updated))

    selected = set(entry.data[CONF_SOURCES]) | {"operating"}
    for entity in er.async_entries_for_config_entry(registry, entry.entry_id):
        if entity.unique_id not in {f"{entry.entry_id}_{key}" for key in selected}:
            registry.async_remove(entity.entity_id)

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
