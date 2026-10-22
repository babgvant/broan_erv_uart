from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]

@pytest.mark.parametrize("passive,pins,options,settings,valid", [
    (False, "  tx_pin: GPIO17\n  flow_control_pin: GPIO21\n", "  experimental_query: true\n  query_uart_direction: true\n  query_source_address: 0x12\n  query_allow_shared_address: true\n  query_recovery_mode: true\n", "", True),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_source_address: 0x12\n  query_allow_shared_address: true\n  query_recovery_mode: true\n", "", True),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_recovery_mode: true\n", "", False),
    (False, "  tx_pin: GPIO17\n", "  flow_control_pin: GPIO21\n  query_recovery_mode: true\n", "", False),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_source_address: 0x12\n  query_allow_shared_address: true\n  query_tolerant_mode: true\n  query_recovery_mode: true\n", "", False),
    (True, "", "  query_recovery_mode: true\n", "", False),
    (True, "", "", "", True),
    (False, "  tx_pin: GPIO17\n", "", "", True),
    (False, "", "", "", False),
    (True, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n", "", False),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n", "", False),
    (False, "", "  experimental_query: true\n  flow_control_pin: GPIO21\n", "", False),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n", "", True),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_tolerant_mode: true\n", "", True),
    (False, "  tx_pin: GPIO17\n", "  query_tolerant_mode: true\n", "", False),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_tolerant_mode: true\n  query_allow_shared_address: true\n  query_source_address: 0x12\n", "", False),
    (False, "  tx_pin: GPIO17\n  flow_control_pin: GPIO16\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n", "", False),
    (False, "  tx_pin: GPIO17\n  flow_control_pin: GPIO21\n", "  experimental_query: true\n  query_uart_direction: true\n", "", True),
    (False, "  tx_pin: GPIO17\n  flow_control_pin: GPIO21\n", "  experimental_query: true\n  query_uart_direction: true\n  query_source_address: 0x12\n  query_allow_shared_address: true\n", "", True),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  query_uart_direction: true\n", "", False),
    (False, "  tx_pin: GPIO17\n  flow_control_pin: GPIO16\n", "  experimental_query: true\n  query_uart_direction: true\n  flow_control_pin: GPIO21\n", "", False),
    (False, "  tx_pin: GPIO17\n  flow_control_pin: GPIO21\n", "  query_uart_direction: true\n", "", False),
    (True, "  tx_pin: GPIO17\n  flow_control_pin: GPIO21\n", "  experimental_query: true\n  query_uart_direction: true\n", "", False),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_source_address: 0x12\n", "", False),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_source_address: 0x12\n  query_allow_shared_address: true\n", "", True),
    (False, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_allow_shared_address: true\n", "", False),
    (False, "  tx_pin: GPIO17\n", "  flow_control_pin: GPIO21\n  query_source_address: 0x12\n  query_allow_shared_address: true\n", "", False),
    (True, "  tx_pin: GPIO17\n", "  experimental_query: true\n  flow_control_pin: GPIO21\n  query_source_address: 0x12\n  query_allow_shared_address: true\n", "", False),
    (True, "  tx_pin: GPIO17\n", "", "", False),
    (True, "", "  flow_control_pin: GPIO21\n", "", False),
    (True, "  flow_control_pin: GPIO21\n", "", "", False),
    (True, "", "  sensor_timeout: 0ms\n", "", False),
    (True, "", "  communication_timeout: 2147483648ms\n", "", False),
    (True, "", "", "  parity: EVEN\n", False),
    (True, "", "", "  data_bits: 7\n", False),
    (True, "", "", "  stop_bits: 2\n", False),
])
def test_uart_configuration(tmp_path, passive, pins, options, settings, valid):
    config = tmp_path / "device.yaml"
    component_path = (ROOT / "components").as_posix()
    config.write_text(f"""esphome:
  name: broan-validation
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
external_components:
  - source:
      type: local
      path: {component_path}
    components: [broan]
uart:
  id: bus
  rx_pin: GPIO18
  baud_rate: 38400
{pins}{settings}broan:
  uart_id: bus
  passive_mode: {str(passive).lower()}
{options}binary_sensor:
  - platform: broan
    operating:
      name: ERV Operating
""")
    result = subprocess.run([sys.executable, "-m", "esphome", "config", str(config)],
                            capture_output=True, text=True)
    assert (result.returncode == 0) == valid, result.stdout + result.stderr

def test_baud_validation(tmp_path):
    config = tmp_path / "wrong-baud.yaml"
    config.write_text(f"""esphome:
  name: broan-baud-validation
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
external_components:
  - source:
      type: local
      path: {(ROOT / 'components').as_posix()}
    components: [broan]
uart:
  id: bus
  rx_pin: GPIO18
  baud_rate: 9600
broan:
  uart_id: bus
  passive_mode: true
""")
    result = subprocess.run([sys.executable, "-m", "esphome", "config", str(config)],
                            capture_output=True, text=True)
    assert result.returncode != 0
    assert "38400" in result.stdout + result.stderr

def test_native_component():
    subprocess.run([sys.executable, str(ROOT / "tests/run_tests.py")], cwd=ROOT, check=True)
