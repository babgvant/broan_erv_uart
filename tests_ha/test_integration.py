"""Exercise flows and entity lifecycle against Home Assistant itself."""

from unittest.mock import AsyncMock, patch

import pytest
from homeassistant.config_entries import SOURCE_RECONFIGURE, SOURCE_USER
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.helpers import entity_registry as er
from pytest_homeassistant_custom_component.common import MockConfigEntry

DOMAIN = "broan_erv"
pytestmark = pytest.mark.usefixtures("sources")


async def setup_entry(hass, sources):
    device, mode, rpm = sources
    entry = MockConfigEntry(
        domain=DOMAIN, title="Broan ERV", unique_id=device.id,
        data={"device_id": device.id, "sources": {"active_mode": mode.id, "exhaust_fan_rpm": rpm.id}},
    )
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    return entry


def entity_id(hass, entry, domain, key):
    return er.async_get(hass).async_get_entity_id(domain, DOMAIN, f"{entry.entry_id}_{key}")


async def test_ui_setup_and_duplicate(hass, sources):
    device, mode, rpm = sources
    with patch("custom_components.broan_erv.async_setup_entry", new=AsyncMock(return_value=True)):
        result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": SOURCE_USER})
        assert result["step_id"] == "user"
        result = await hass.config_entries.flow.async_configure(
            result["flow_id"], {"name": "Broan ERV", "device_id": device.id},
        )
        assert result["step_id"] == "sources"
        result = await hass.config_entries.flow.async_configure(
            result["flow_id"], {"active_mode": mode.entity_id, "exhaust_fan_rpm": rpm.entity_id},
        )
        assert result["type"] is FlowResultType.CREATE_ENTRY
        assert result["data"]["sources"]["active_mode"] == mode.id
        await hass.async_block_till_done()
        result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": SOURCE_USER})
        result = await hass.config_entries.flow.async_configure(
            result["flow_id"], {"name": "Duplicate", "device_id": device.id},
        )
        assert result["reason"] == "already_configured"


async def test_states_and_unload(hass, sources):
    _, mode, rpm = sources
    entry = await setup_entry(hass, sources)
    operating = entity_id(hass, entry, "binary_sensor", "operating")
    active = entity_id(hass, entry, "sensor", "active_mode")
    exhaust = entity_id(hass, entry, "sensor", "exhaust_fan_rpm")
    assert hass.states.get(operating).state == "off"
    assert hass.states.get(exhaust).state == "0.0"
    for value, expected in [("Low", "on"), ("2", "on"), ("Idle", "off"),
                            ("222", "unknown"), ("Unknown", "unavailable"),
                            ("unavailable", "unavailable"), ("Turbo", "on")]:
        hass.states.async_set(mode.entity_id, value)
        await hass.async_block_till_done()
        assert hass.states.get(operating).state == expected
    hass.states.async_set(mode.entity_id, "2")
    hass.states.async_set(rpm.entity_id, "NaN", {"unit_of_measurement": "RPM"})
    await hass.async_block_till_done()
    assert hass.states.get(active).state == "High"
    assert hass.states.get(exhaust).state == "unavailable"
    assert await hass.config_entries.async_unload(entry.entry_id)
    hass.states.async_set(mode.entity_id, "Idle")
    await hass.async_block_till_done()
    assert hass.states.get(operating) is None


async def test_source_rename_and_removal(hass, sources):
    _, mode, _ = sources
    entry = await setup_entry(hass, sources)
    operating = entity_id(hass, entry, "binary_sensor", "operating")
    registry = er.async_get(hass)
    renamed = registry.async_update_entity(mode.entity_id, new_entity_id="sensor.renamed_erv_mode")
    hass.states.async_set(renamed.entity_id, "High")
    await hass.async_block_till_done()
    assert entity_id(hass, entry, "binary_sensor", "operating") == operating
    assert hass.states.get(operating).state == "on"
    assert hass.states.get(operating).attributes["source_entity"] == renamed.entity_id
    registry.async_remove(renamed.entity_id)
    await hass.async_block_till_done()
    assert hass.states.get(operating).state == "unavailable"


async def test_reconfigure_removes_optional_sensor(hass, sources):
    _, mode, _ = sources
    entry = await setup_entry(hass, sources)
    exhaust = entity_id(hass, entry, "sensor", "exhaust_fan_rpm")
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_RECONFIGURE, "entry_id": entry.entry_id},
    )
    assert result["step_id"] == "sources"
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"active_mode": mode.entity_id})
    assert result["reason"] == "reconfigure_successful"
    await hass.async_block_till_done()
    assert er.async_get(hass).async_get(exhaust) is None
    assert entry.data["sources"] == {"active_mode": mode.id}


async def test_native_operating_source(hass, sources):
    device, mode, _ = sources
    registry = er.async_get(hass)
    source_entry = hass.config_entries.async_get_entry(mode.config_entry_id)
    native = registry.async_get_or_create(
        "binary_sensor", "esphome", "erv-operating", config_entry=source_entry,
        device_id=device.id, original_name="Operating", suggested_object_id="erv_operating",
    )
    hass.states.async_set(native.entity_id, "off")
    hass.states.async_set(mode.entity_id, "Low")
    entry = MockConfigEntry(
        domain=DOMAIN, title="Broan ERV", unique_id=device.id,
        data={"device_id": device.id, "sources": {"active_mode": mode.id, "operating": native.id}},
    )
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    operating = entity_id(hass, entry, "binary_sensor", "operating")
    assert hass.states.get(operating).state == "off"
    hass.states.async_set(native.entity_id, "on")
    await hass.async_block_till_done()
    assert hass.states.get(operating).state == "on"
    hass.states.async_set(native.entity_id, "unknown")
    await hass.async_block_till_done()
    assert hass.states.get(operating).state == "unavailable"
