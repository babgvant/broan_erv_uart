"""Run these tests on Linux with pytest-homeassistant-custom-component."""

from unittest.mock import AsyncMock, patch

import pytest
from homeassistant.helpers import device_registry as dr, entity_registry as er
from pytest_homeassistant_custom_component.common import MockConfigEntry


@pytest.fixture(autouse=True)
def custom_integrations(enable_custom_integrations):
    yield


@pytest.fixture
def sources(hass):
    entry = MockConfigEntry(domain="esphome", title="ERV monitor", data={"host": "192.0.2.1"})
    entry.add_to_hass(hass)
    device = dr.async_get(hass).async_get_or_create(
        config_entry_id=entry.entry_id, identifiers={("esphome", "erv-monitor")}, name="ERV monitor",
    )
    registry = er.async_get(hass)
    mode = registry.async_get_or_create(
        "sensor", "esphome", "erv-mode", config_entry=entry,
        device_id=device.id, original_name="Active Mode", suggested_object_id="erv_active_mode",
    )
    rpm = registry.async_get_or_create(
        "sensor", "esphome", "erv-rpm", config_entry=entry,
        device_id=device.id, original_name="Exhaust Fan RPM", suggested_object_id="erv_exhaust_fan_rpm",
    )
    hass.states.async_set(mode.entity_id, "Idle")
    hass.states.async_set(rpm.entity_id, "0", {"unit_of_measurement": "RPM"})
    with (
        patch("homeassistant.components.esphome.async_setup_entry", new=AsyncMock(return_value=True)),
        patch("homeassistant.components.esphome.async_unload_entry", new=AsyncMock(return_value=True)),
    ):
        yield device, mode, rpm
