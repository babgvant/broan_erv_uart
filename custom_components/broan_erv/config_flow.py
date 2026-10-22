"""Select existing ESPHome telemetry from the Home Assistant UI."""

import probatio as vol

from homeassistant.config_entries import ConfigFlow
from homeassistant.const import CONF_NAME
from homeassistant.helpers import device_registry as dr, entity_registry as er, selector

from .const import CONF_DEVICE, CONF_SOURCES, DOMAIN, SOURCES, SOURCE_BY_KEY, matches_source


class BroanConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        if user_input is not None:
            self._device_id = user_input[CONF_DEVICE]
            self._name = user_input[CONF_NAME].strip() or "Broan ERV"
            await self.async_set_unique_id(self._device_id)
            self._abort_if_unique_id_configured()
            return await self.async_step_sources()
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({
                vol.Required(CONF_NAME, default="Broan ERV"): selector.TextSelector(),
                vol.Required(CONF_DEVICE): selector.DeviceSelector({"filter": {"integration": "esphome"}}),
            }),
        )

    async def async_step_reconfigure(self, user_input=None):
        entry = self._get_reconfigure_entry()
        self._device_id = entry.data[CONF_DEVICE]
        self._name = entry.title
        return await self.async_step_sources(user_input)

    async def async_step_sources(self, user_input=None):
        registry = er.async_get(self.hass)
        candidates = {
            entity.entity_id: entity
            for entity in er.async_entries_for_device(registry, self._device_id)
            if entity.platform == "esphome" and not entity.disabled
            and entity.domain in ("sensor", "binary_sensor")
        }
        if dr.async_get(self.hass).async_get(self._device_id) is None:
            return self.async_abort(reason="device_missing")
        if not any(entity.domain == "sensor" for entity in candidates.values()):
            return self.async_abort(reason="no_sources")

        errors = {}
        if user_input is not None:
            for key, entity_id in user_input.items():
                entity = candidates.get(entity_id)
                if key not in SOURCE_BY_KEY or entity is None or entity.domain != SOURCE_BY_KEY[key].domain:
                    errors[key] = "invalid_source"
            if "active_mode" not in user_input:
                errors["active_mode"] = "required_source"
            if not errors:
                data = {CONF_DEVICE: self._device_id, CONF_SOURCES: {
                    key: candidates[entity_id].id for key, entity_id in user_input.items()
                }}
                if self.source == "reconfigure":
                    return self.async_update_reload_and_abort(self._get_reconfigure_entry(), data=data)
                return self.async_create_entry(title=self._name, data=data)

        suggestions = {}
        if self.source == "reconfigure":
            for key, source_id in self._get_reconfigure_entry().data[CONF_SOURCES].items():
                if entity := registry.async_get(source_id):
                    suggestions[key] = entity.entity_id
        else:
            for definition in SOURCES:
                matches = [entity.entity_id for entity in candidates.values()
                           if entity.domain == definition.domain and matches_source(
                               definition, entity.original_name or entity.name or entity.entity_id.split(".")[1])]
                if len(matches) == 1:
                    suggestions[definition.key] = matches[0]
        if user_input is not None:
            suggestions = user_input
        schema = {}
        for definition in SOURCES:
            marker = vol.Required if definition.key == "active_mode" else vol.Optional
            field = marker(definition.key, description={"suggested_value": suggestions.get(definition.key)})
            schema[field] = selector.EntitySelector({"include_entities": [
                entity.entity_id for entity in candidates.values() if entity.domain == definition.domain
            ]})
        return self.async_show_form(step_id="sources", data_schema=vol.Schema(schema), errors=errors)
