# Home Assistant entities and freshness

All Broan telemetry depends on a validated `21` response from ERV address `0x10`. An entity in the YAML is a supported candidate, not evidence that the VTTOUCH polls it. Remove candidate entities that stay unobserved after discovery. No polling is performed to fill gaps.

## Telemetry

The example's friendly name is ADU ERV. Home Assistant generally generates the entity IDs below; check the actual registry, especially if entities already exist.

| Example entity ID | Broan configuration key | Register | Unit |
| --- | --- | --- | --- |
| `sensor.adu_erv_power` | `power` | `2350` | W |
| `sensor.adu_erv_intake_temperature` | `temperature` | `01E0` | °C |
| `sensor.adu_erv_exhaust_temperature` | `temperature_out` | `03E0` | °C |
| `sensor.adu_erv_filter_life` | `filter_life` | `0830` | d, seconds / 86400.0 |
| `sensor.adu_erv_supply_fan_cfm` | `supply_fan_cfm` | `0510` | CFM |
| `sensor.adu_erv_exhaust_fan_cfm` | `exhaust_fan_cfm` | `0610` | CFM |
| `sensor.adu_erv_supply_fan_rpm` | `supply_fan_rpm` | `0310` | RPM |
| `sensor.adu_erv_exhaust_fan_rpm` | `exhaust_fan_rpm` | `0410` | RPM |
| `sensor.adu_erv_base_mode_code` | `base_mode_code` | `0220` | raw numeric code |
| `sensor.adu_erv_active_mode_code` | `active_mode_code` | `0720` | raw numeric code |
| `sensor.adu_erv_fault_code` | text `fault_code` | `1700` | `OK` or `E<number>` |
| `sensor.adu_erv_warning_code` | text `warning_code` | `1A00` | `OK` or `W<number>` |
| `sensor.adu_erv_active_mode` | text `active_mode` | `0720` | Idle, Low, High, Turbo, Medium, Bath Override, or raw code |
| `sensor.adu_erv_base_mode` | text `base_mode` | `0220` | raw code as text |
| `sensor.adu_erv_model` | text `model` | `0260` | identity string |
| `sensor.adu_erv_firmware` | text `firmware` | `0200` | identity string |
| `sensor.adu_erv_firmware_version` | text `firmware_version` | `0100` | dotted bytes |
| `sensor.adu_erv_hardware_revision` | text `hardware_revision` | `0160` | dotted bytes |
| `binary_sensor.adu_erv_fault_active` | `fault_active` | `1700` | true for any code except `FFFFFFFF` |
| `binary_sensor.adu_erv_operating` | `operating` | `0720` | false for 0; true for 1–5; unknown for unrecognized codes or missing/stale telemetry |
| `sensor.adu_erv_last_fault` | text `last_fault` | `1700` | last non-OK code and boot-relative observation time |

Numeric `fault_code` and `warning_code` are also supported by the sensor platform, with −1 representing OK. They are separate from the existing text platform with the same keys. The previously declared numeric diagnostic schema now has working C++ setters and publishing.

The current mode labels are Idle, Low, High, Turbo, Medium, and Bath Override for codes 0–5. See [operation history](history.md) for a timeline and runtime totals. The `operating` binary sensor follows the reported active mode rather than the configured base mode or measured RPM. Its per-register timeout applies in active controller mode as well as passive/query modes.

## Freshness behavior

Only successful register decoding refreshes that register. Request traffic, write commands, unknown messages, corrupted frames, and unrelated telemetry cannot keep an old RPM value fresh. Equal values refresh their receive time and republish, including zero on the first observed sample. Freshness timestamps are independent of active polling timestamps and use unsigned elapsed time across clock wrap.

In passive mode, numeric entities publish NaN at startup and after `sensor_timeout` (default 300 s). The native API represents that as Home Assistant **unknown**. Fault binary state is invalidated to unknown. ESPHome 2026.9.1 text sensors lack an invalidation API, so current text telemetry changes to the literal **Unknown**. Identity strings follow the same per-register timeout and may go stale if the controller only reads them at startup. `last_fault` intentionally retains historical information.

For explicit Home Assistant **unavailable** states, use the availability templates in [home-assistant-package.yaml](../examples/home-assistant-package.yaml), which demonstrate numeric fan and text fault/mode wrappers. Apply the same `has_value()` availability expression to any other numeric entity you want to normalize. Alerts use availability checks and never convert missing values to zero.

`communication_ok` is true while any checksum/footer-valid bus packet has arrived within `communication_timeout` (default 120 s). It starts false. This measures **bus activity**, not ERV health: controller-only requests can keep it true after the ERV stops responding. Per-register expiry remains the ERV telemetry freshness check. Lost Wi-Fi/API connectivity makes the device's entities unavailable independently of both timeouts.

Freshness applies to passive telemetry. Active mode retains its established scheduling and control API. Do not expose select, number, switch, or filter-reset controls on the passive device; the component ignores their methods if called.

## Diagnostic entities

The device example exposes template sensors backed by component counters, and a native communication binary sensor. Counters restart at boot.

| Entity suffix | Meaning | Update interval |
| --- | --- | --- |
| `rs485_packets_received` | Frame candidates processed, including rejected candidates and timeouts | 60 s |
| `rs485_valid_packets` | Checksum/footer-valid frames, including commands without telemetry | 60 s |
| `rs485_invalid_packets` | Rejected frame candidates, not an exact count of damaged original packets | 60 s |
| `rs485_checksum_failures` | Candidates rejected for checksum mismatch | 60 s |
| `rs485_unknown_registers` | Unknown register records in structurally valid read responses | 60 s |
| `rs485_last_valid_packet_age` | Seconds since any valid frame; unknown before first frame | 10 s |
| `rs485_communication` | Fresh bus activity | Each component loop |
| `device_online` | Native API device connection status | Native status component |
| `restart_monitor` | Reboots only the monitoring ESP32 | Button |

`log_packets`, `log_unknown_registers`, and `register_discovery` are disabled by default. Packet logs include direction, message classification and full frame hex at INFO. Unknown register details use DEBUG, so set logger level DEBUG for that option. Discovery emits cumulative request/response counts per wire ID once per minute at INFO. Differences between summaries give frequencies. It is passive discovery and never scans registers by transmitting. Checksum-valid malformed register payloads count as valid **frames**, but do not refresh telemetry.

Watts are instantaneous reported power. Energy requires integrating fresh power samples in Home Assistant; the component does not invent missing power or energy during gaps.
