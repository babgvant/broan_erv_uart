# Passive receiver validation and field capture

Bench-test the physical receive-only barrier before attaching the monitor to the operating ERV. A passing software test establishes component behavior, not transceiver electrical behavior.

## Build and test

Use ESPHome 2026.9.1, the version used for configuration validation. Create `examples/secrets.yaml` from the example, set Wi-Fi credentials, and generate a unique 32-byte base64 API key. The passive example requires that key for native API and encrypted OTA. Never commit the secrets file.

```text
python -m pip install -r tests/requirements.txt
python -m pytest -q tests --basetemp tests/build/pytest
python -m esphome config examples/active-controller.yaml
python -m esphome config examples/adu-broan-erv.yaml
python -m esphome compile examples/active-controller.yaml
python -m esphome compile examples/adu-broan-erv.yaml
```

The active example completes the existing humidity-control configuration using local component sources; it also exercises intermittent-period support. It is a build fixture for replacing a serial controller, and must not be flashed onto a tap connected to an existing VTTOUCH bus.

The native tests compile the real `broan.cpp`, `broan_control.cpp` and `broan_monitor.cpp` against minimal ESPHome entities and a mock UART. They exercise valid reads, different addresses, unsolicited responses, partial and consecutive frames, checksum failures, unknown registers, malformed values, lost responses, resynchronization, float and filter-day conversion, fault formatting, retention, equal-value recovery, first zero samples, register expiry, bus expiry, clock wrap, passive controls producing zero writes/flushes/direction operations, and active handshake/write/poll regression. Schema tests check RX-only acceptance, active TX requirements, passive TX/direction rejection, baud, parity, data bits and stop bits. On Windows the runner requires Visual Studio C++ Build Tools; on Linux it uses a C++20 compiler with address and undefined-behavior sanitizers.

Validation on October 13, 2026: both complete examples passed configuration validation and firmware compilation with ESPHome 2026.9.1 and ESP-IDF 5.5.5. The suite passed 33 tests, including Home Assistant template syntax, stale-data alert guards, and retained timestamp behavior on reconnect. Native tests ran with MSVC C++20. No ERV or Waveshare hardware was connected, and the Home Assistant package was checked as YAML and rendered templates rather than loaded into a running Home Assistant instance.

## Electrical bench procedure

1. Keep the ERV disconnected from the test setup. Identify the purchased board revision and continuity-check the schematic against U7, U9, R22, H2, R24 and R27. Carry out the rework in [wiring.md](wiring.md). Check both U7 enable pins connect to SGND and are isolated from the original control node. Confirm termination and added bias are disconnected.
2. Build an independent, normally terminated RS485 bench bus with a USB adapter and another receiver or logic analyzer. Check baseline idle differential voltage and decoded packets without the monitor. Power the monitor from its own supply; connect A/B as a parallel tap.
3. Use an isolated differential probe on A/B, and a properly referenced probe on U7 DE and `/RE`. Account for instrument grounds so probing does not bridge the board's isolation. Both enable pins must remain low relative to SGND throughout power-up, reset, bootloader entry, programming, OTA, watchdog reset and normal operation.
4. With the bench adapter driver disabled, verify the monitor never drives the pair. Repeat while deliberately toggling GPIO17/TX and GPIO21 with bench-only test firmware. A lack of intentional transmissions from the production component is insufficient; toggling both signals tests the barrier itself. Restore the passive firmware before installation.
5. Enable the adapter driver and send synthetic valid and malformed Broan frames at 38400 8N1. Compare waveforms and decoded traffic with the baseline. Confirm monitor telemetry changes only for validated ERV responses, packet counters increase, and malformed frames recover. The mocked C++ tests do not replace this electrical test.
6. Switch the monitor off while bench traffic continues. Confirm no bus clamping, unexplained loading, new frame errors, or back-powering. Measure relative to the same baseline. Disconnect the tap and confirm the original path remains intact. Use a different receiver if it fails this test.
7. Restore production YAML, verify `passive_mode: true`, RX-only UART and logger baud rate zero, and label the device as a receive-only monitor. Keep the hardware barrier in place during all future firmware changes.

A correctly disabled SP3485 driver is high impedance with DE low; the receiver is enabled with `/RE` low. This follows the [MaxLinear SP3485 datasheet](https://www.maxlinear.com/ds/sp3485.pdf). Powered-off loading must still be checked for the complete board and protection circuitry.

## Live installation

Record the normal VTTOUCH mode, both VBATHW responses and ventilation state first. Add the short parallel tap with no original wiring routed through it. Leave existing termination and bias unchanged. Power the monitor independently. Confirm mode changes from the VTTOUCH and operation of each VBATHW individually. Power off, restart and disconnect the monitor while checking that the existing controls continue operating. Do not continue if behavior changes.

Compare observed measured RPM, CFM, temperature and power with plausible operating states; commanded flow targets are separate registers. Set standby and intermittent idle through the VTTOUCH to establish when zero RPM is expected. Restore the original operating mode afterward. Verify sensor timeouts by disconnecting only the monitor's tap while its API remains online. Values must become unknown after their own timeout; a running value must never turn into an invented zero.

## Capture VTTOUCH traffic

After the electrical checks, temporarily enable:

```yaml
logger:
  baud_rate: 0
  level: DEBUG
  tx_buffer_size: 1024
broan:
  uart_id: broan_uart
  passive_mode: true
  log_packets: true
  log_unknown_registers: true
  register_discovery: true
  communication_timeout: 120s
  sensor_timeout: 300s
```

These are changes to the existing blocks, not duplicate top-level blocks. Capture ESPHome logs over Wi-Fi/API to a file, for example:

```text
python -m esphome logs examples/adu-broan-erv.yaml --device adu-broan-erv.local > vttouch-capture.txt
```

Collect at least 30 minutes of normal operation, several safe mode changes, each VBATHW override, and an intermittent run/idle cycle. Use a longer capture through normal defrost conditions before enabling fan alerts. Record real observation times, ERV model/firmware, board revision and VTTOUCH displayed mode alongside the logs. Keep logger TX disabled. Discovery prints a summary each minute; subtract successive cumulative counts to derive polling frequency and measure the longest response gap for each entity. Choose timeouts comfortably above those gaps.

Look specifically for read requests containing `17 00` and `1A 00`, and ERV responses with those IDs. During any naturally occurring E38, save logs before power-cycling if practical, plus VTTOUCH/display observations. Determine whether the register reports decimal 38, whether it changes without a request, how long fault reads take, and whether it clears or persists after recovery. Capture the controller's initialization only when an ERV restart is already necessary; monitoring should not require disturbing ventilation.

Enable long packet logging only for investigation; turn it off afterward. UART throughput or log buffering can drop frames, so externally captured raw RS485 traffic is useful if log counts suggest losses. Use only an electrically verified receive-only capture interface.

## Alert commissioning

Install [home-assistant-package.yaml](../examples/home-assistant-package.yaml) using the [Home Assistant package mechanism](https://www.home-assistant.io/docs/configuration/packages/) and run Home Assistant's configuration check before restarting. Verify every referenced entity ID. The examples create dashboard notifications; mobile push actions can be substituted locally. For example, after copying it into the Home Assistant configuration directory, merge this into the existing `homeassistant` block:

```yaml
homeassistant:
  packages:
    adu_erv: !include home-assistant-package.yaml
```

The [availability templates](https://www.home-assistant.io/integrations/template/) mark stale wrappers unavailable. Native entities remain useful for diagnostics and history. [Template trigger](https://www.home-assistant.io/docs/automation/trigger/) delays restart when Home Assistant restarts or automations reload; alert examples do not provide durable timers.

Fan alerts start disabled. The allowlist uses continuous base modes 9/10/11 and active codes 1/2/4 from the repository's observed mappings, excluding standby, intermittent idle, recirculation, away, humidity, turbo and unknown states. Confirm your device's mapping across startup, VBATHW overrides and defrost before enabling the helper. If a model legitimately stops either fan while retaining an allowlisted code, remove that code or add a verified defrost/maintenance exclusion. No confirmed defrost register is available here; the allowlist alone cannot establish every model's behavior. Adjust the 100 RPM threshold and two-minute delay to field data.

The retained-fault helper timestamp is the Home Assistant receipt time, not a clock supplied by the ERV. Keep the helpers and last-fault entity in Recorder. Helpers survive monitor restarts; component retention is RAM only. If Home Assistant was offline, it records the time it receives the retained sample rather than reconstructing a historical wall-clock time from monitor uptime.

## Field questions and limits

- Which supported IDs are requested by this VTTOUCH, at what intervals, and with what lengths? Registers never observed cannot populate passive sensors, even though active mode can poll them.
- Does the installed ERV use the repository's fault numbering, and does E38 appear in `1700`? Manufacturer display semantics alone do not prove bus availability. The [Broan AI series manual](https://www.broan-nutone.com/getmedia/b44db6e1-7cec-4416-b839-08f2a1775fc8/Installation-Guide-Users-Manual-AI-Series.pdf) identifies E38 as exhaust motor speed feedback, but it does not document this RS485 register mapping.
- Does the controller keep requesting after ERV responses cease? Bus activity may stay fresh while telemetry expires.
- Do initialization-only identity reads occur again? Model/firmware entities can intentionally become stale.
- Which modes allow one or both fans to stop, including defrost? Do not treat stale RPM as fan stoppage.
- Unknown command formats remain opaque. Unsupported registers are logged without speculative type or engineering-unit assignments.

Actual operation of the VTTOUCH and both VBATHW devices, physical transmitter disablement, and powered-off noninterference require the bench and field checks above. Firmware and synthetic packet tests cannot establish those physical outcomes.
