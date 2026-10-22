"""Portable checks for the HACS companion's decoding and package metadata."""

import importlib.util
import json
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
INTEGRATION = ROOT / "custom_components/broan_erv"
spec = importlib.util.spec_from_file_location("broan_erv_test_const", INTEGRATION / "const.py")
model = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = model
spec.loader.exec_module(model)


@pytest.mark.parametrize("value,expected", [
    ("Idle", False), ("0", False), ("0.0", False),
    ("Low", True), ("High", True), ("Turbo", True), ("Medium", True),
    ("Bath Override", True), ("  low  ", True),
    ("1", True), ("2.0", True), ("3", True), ("4", True), ("5", True),
    ("222", None), ("6", None), ("-1", None), ("1.5", None),
    ("Unknown", None), ("unknown", None), ("unavailable", None),
    (None, None), ("", None), ("NaN", None), ("inf", None), ("Running", None),
])
def test_operating_state(value, expected):
    assert model.operating_value(value) is expected


@pytest.mark.parametrize("value,expected", [
    ("0", 0.0), ("1250", 1250.0), ("42.5", 42.5),
    ("unknown", None), ("unavailable", None), (None, None),
    ("NaN", None), ("inf", None), ("-inf", None), ("garbled", None),
])
def test_numeric_state(value, expected):
    assert model.numeric_value(value) == expected


def test_labels_and_source_suggestions():
    assert model.mode_label("active_mode", "2.0") == "High"
    assert model.mode_label("base_mode", "8") == "Intermittent"
    assert model.mode_label("active_mode", "222") == "222"
    assert model.matches_source(model.SOURCE_BY_KEY["active_mode"], "ADU ERV Active Mode")
    assert not model.matches_source(model.SOURCE_BY_KEY["active_mode"], "Active Mode Code")
    assert not model.matches_source(model.SOURCE_BY_KEY["power"], "Power Supply")


def test_hacs_metadata_and_translations():
    hacs = json.loads((ROOT / "hacs.json").read_text())
    manifest = json.loads((INTEGRATION / "manifest.json").read_text())
    strings = json.loads((INTEGRATION / "strings.json").read_text())
    assert hacs["render_readme"] is True
    assert manifest["domain"] == model.DOMAIN
    assert manifest["config_flow"] is True
    assert manifest["dependencies"] == ["esphome"]
    assert set(strings["config"]["step"]["sources"]["data"]) == set(model.SOURCE_BY_KEY)
    assert strings == json.loads((INTEGRATION / "translations/en.json").read_text())
