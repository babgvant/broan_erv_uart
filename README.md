# Broan ERV serial component

An ESP32 component to communicate with Broan, Nutone, Venmar, and VanEE ERVs via their rs485 interface.

## Home Assistant / HACS

<img src="custom_components/broan_erv/brand/icon.png" alt="Broan ERV airflow and heat-exchanger icon" width="96" height="96">

The [Broan ERV companion integration](docs/hacs.md) can be installed through HACS as a custom repository. Select your existing ESPHome ERV device and telemetry in the HA setup UI, then manage the imported sensors under a dedicated Broan ERV integration. It includes an Operating sensor for activity history. HACS manages the HA companion; the UART firmware still runs on the ESP32 and is updated through ESPHome.

The protocol is documented [here](https://spitko.net/2025/08/08/Reverse-Engineering-an-ERV/)

## Passive monitoring

This fork adds `passive_mode: true` for observing an existing VTTOUCH without joining the controller exchange. The default remains the original active controller mode. Passive mode requires an RX-only UART and ignores all control methods.

Use the complete [ADU Waveshare example](examples/adu-broan-erv.yaml), after completing the required [receive-only hardware modification and wiring checks](docs/wiring.md). The stock board's direction GPIO does not guarantee electrical silence during boot or failure. The coexistence and controller-power warnings in the original requirements below apply to **active controller replacement**, rather than passive observation.

See [protocol and architecture](docs/protocol.md), [entities and freshness](docs/entities.md), [validation and traffic capture](docs/validation.md), and [Home Assistant alerts](examples/home-assistant-package.yaml). Candidate telemetry appears only when the existing ERV traffic carries it; VTTOUCH polling and E38 availability still require field captures.

To see when the ERV ran earlier today, follow [operation history in Home Assistant](docs/history.md). It includes an active/idle timeline and runtime helpers.

Currently this project is stable, but may be missing some advanced features from certain models. If there is a feature you'd like added, please open an issue. We may need packet capture from a unit with this functionality.

<table>
  <tr>
    <td>
      <img width="332" height="477" alt="image" src="https://github.com/user-attachments/assets/5170773c-7def-4df2-bef0-9fa33ca78082" />
    </td>
    <td valign="top">
      <img width="329" height="367" alt="image" src="https://github.com/user-attachments/assets/2e4aba9a-a7a8-4349-9e79-a22f0018b23a" />
    </td>
  </tr>
</table>

## Supported models
We currently don't have a good range of what does and don't work, but it's currently believed that all Broan, Nutone, and VanEE ERVs that have rs485 inputs probably work. The easiest way to check is to see if your unit supports the VTTOUCHW (All three brands have a version of this interface, they are all presumed to be the same)

HRVs have been reported to work as well, but tend to support fewer features. You can safely remove sensors from the yaml that your device does not support.

## Requirements
1) You will need an esphome device that can communicate over rs485. Some devices come with this out of the box (waveshare esp32-s3-relay-6ch), or you can just buy an external tranceiver and wire it to the uart of your choice. Note that the waveshare device listed earlier is much easier to use than most, as it has automatic flow control and can be powered directly from the 12v output on your ERV. If you use a different device and make a working, stable configuration, please open an issue so we can start building a list with configuration files.
2) This library does not coexist with other serial wall remotes. This is a software limitation on the ERV itself, it will only ever respond to one device on the bus. It's theoretically possible to MITM a remote but that's outside the scope of this component.
3) You will ideally want to power this directly from the 12v output on the erv itself. this isn't a hard requirement, but it simplifies things a lot. If the ERV completes the handshake with the esp32 and it later goes away, the ERV will eventually drop into an error state and shut down, so it's just one less point of failure.

## Installation
Near the control interface on the ERV, look for a green terminal block that has D+, D-, and GND on it (typically a 6 terminal block that includes 12V, LED, and OVR). Connect the D+, D-, and GND connectors to your RS485 tranceiver. Ideally you can also use the +12v but very few ESP32s are set up to handle input voltages above 5V to check with your spec sheet first. If not, just use a USB cable and wall charger. If you go this route, you may need to additionally run a wire from the GND terminal on the ERV to the GND pin on the esp32, but this will depend on how your rs485 tranceiver is set up. Only do this if you're getting unexplained communication errors or crashes.

Some rs485 trancevers have a jumper for the terminating resistor, some do not. In my case I found I did not need a termination resister, but you may need one. Try enabling this, or adding a resistor manually across the terminals on the ESP32 side, if you have communication issues. You'll know if you flip these because you'll see a bunch of spew about alignment errors and unknown commands.

Also be aware some RS485 devices will label their pins A and B instead of D+ and D-. Somewhat confusingly, A is D- and B is D+

## Supported features
* Setting fan mode (Standby, Min, Max, Intermittent, Turbo, Override, and Med, which is treated as manual control)
* Setting fan speed in manual mode
* Humidity control mode
* Intake temperature
* Filter life left
* Fan CFM 

More features will be added as time allows. I've documented many fields that aren't supported yet. If there's a specific feature you want prioritized, open an issue. This project is at a point where it "works for me" so I don't have a lot of guiding light on what else should be added without external input.

## Humidity Control Mode
In Humidity Control Mode, the controller sets a target humidity level and the ERV automatically runs if the humidity is above this level. The ERV does not have a humidity sensor - the current humidity reading is sent periodically from the controller.  Since we are replacing the original controller, we need to send current humidity readings from the ESP device. This can be done from a lambda in your ESPHome config calling the setCurrentHumidity() function.  For an example configuration, see the [humidity_control_sample.yaml](./examples/humidity_control_example.yaml) configuration in the [examples](./examples/) directory.

To use humidity control mode once it is enabled, set the desired humidity with "Humidity Setpoint", and then turn on the Humidity Control switch.

## FAQ
Q: I see errors about failed communication

A: This could be a lot of things.
- If you're getting timeouts, it's probably the yaml being misconfigured. A lot of rs485 devices want a flow control pin, which needs to be specified (check your device's datasheet)
- If you see it getting data but complaining about alignment and unknown commands, it's likely either you swapped the +/- wires, or the communication is very weak. Double check that you wired up the ground wire correctly, and try adding or removing esp side termination.

Q: Why doesn't it support X?

A: Not everything is actually exposed via the rs485 interface. I may need dumps from a unit with the feature you're requesting, or it might already be mapped and just needs to be plumbed though to Home Assistant. Feel free to open an issue or PR

Q: What if I still want wall controls?

A: You can use the aux remotes, those use the dry contact interface which is a hard override. The fan mode will indicate "ovr" (override) when these controls are used. This is how Broan bypasses the protocol limitation. Alternatively, look at something like the Sonoff NSPanel and control the device via Home Assistant instead.

## YAML configuration reference

The settings below cover the custom `broan` component and every `platform: broan` entity. Normal ESPHome device settings (`wifi`, `api`, `ota`, `logger`, etc.) remain part of your device configuration; complete board configurations are in [examples](examples/).

### UART and operating mode

The linked `uart` must use **38400 baud, 8 data bits, no parity, and 1 stop bit**. Set `rx_pin` for every mode and `tx_pin` for active controller or experimental query mode. Pin numbers depend on your board. The examples use `rx_buffer_size: 2048`. If the logger shares the serial port, disable serial logging with `logger: {baud_rate: 0}`.

| `broan:` setting | Default | Meaning |
| --- | --- | --- |
| `id` | Generated | Component ID for `broan_id` references and lambdas such as `id(erv).setCurrentHumidity(...)`. |
| `uart_id` | Inferred when unambiguous | ID of the UART connected to the RS485 transceiver. Set it explicitly when multiple UARTs exist. |
| `flow_control_pin` | Unset | GPIO output controlling the transceiver's transmit enable. Configure when required by your active-controller hardware. Required for experimental queries unless `query_uart_direction` is enabled. Forbidden in passive mode. |
| `passive_mode` | `false` | Observe the existing controller without transmitting. Requires an RX-only UART with neither `tx_pin` nor `uart.flow_control_pin`. Control methods are ignored. Complete the [receive-only hardware modification](docs/wiring.md) before using this mode. |
| `communication_timeout` | `120s` | Maximum age of any valid bus frame for `communication_ok` to remain true. This measures bus activity; controller requests alone can keep it true. |
| `sensor_timeout` | `300s` | Maximum age of each decoded telemetry register in passive/query monitoring. Numeric values become unknown, current text becomes `Unknown`, and fault/humidity binary states are invalidated. Identity fields also expire; `last_fault` retains history. Countdown sensors use the shorter of this value and 10 seconds. |
| `log_packets` | `false` | Log full packet hex, direction, and classification at INFO. |
| `log_unknown_registers` | `false` | Log unknown register details at DEBUG; requires `logger.level: DEBUG` to see them. |
| `register_discovery` | `false` | Observe register traffic and log cumulative request/response counts and diagnostics once per minute at INFO. Does not transmit a register scan. |
| `experimental_query` | `false` | Enable experimental read probes alongside an existing VTTOUCH. Requires `passive_mode: false` and transmit-capable hardware. Normal controller writes are disabled. See query settings below. |
| `query_source_address` | `0x13` | Query source address, from `0x12` through `0x1F`. Address `0x12` requires explicit shared-address opt-in. |
| `query_allow_shared_address` | `false` | Permit using the VTTOUCH address. Requires `experimental_query: true` and `query_source_address: 0x12`. |
| `query_uart_direction` | `false` | Let the UART driver control RS485 transmit direction for experimental queries. Requires `experimental_query: true` and `uart.flow_control_pin`; omit `broan.flow_control_pin`. |
| `query_tolerant_mode` | `false` | Query mode with a 1500 ms reply timeout and a limited parser-error budget instead of strict 500 ms handling. Requires experimental queries with a separate source address; cannot use `query_allow_shared_address`. |
| `query_recovery_mode` | `false` | Enable shared-address query recovery handling, including pause/backoff during continuous polling. Requires experimental queries, `query_allow_shared_address: true`, and `query_tolerant_mode: false`. |

Both timeouts accept ESPHome duration values from `1ms` through `2147483647ms`. Defaults select active controller replacement. Passive mode and experimental queries are mutually exclusive. Experimental queries can disturb the existing controller exchange; use the dedicated examples and observe `query_status` when testing.

With software query direction (the default), configure `broan.flow_control_pin` and omit `uart.flow_control_pin`. With `query_uart_direction: true`, configure only `uart.flow_control_pin`. The following examples demonstrate the supported combinations:

| Example | Purpose |
| --- | --- |
| [adu-broan-erv.yaml](examples/adu-broan-erv.yaml) | Passive monitoring on the ADU Waveshare board. |
| [active-controller.yaml](examples/active-controller.yaml) | Active controller replacement with controls and humidity input. |
| [experimental-query.yaml](examples/experimental-query.yaml) | Separate-address, strict queries using local component sources. |
| [adu-broan-erv-query.yaml](examples/adu-broan-erv-query.yaml) | Separate-address, tolerant queries using Git sources. |
| [adu-broan-erv-shared-query.yaml](examples/adu-broan-erv-shared-query.yaml) | Shared-address, strict queries and an opt-in telemetry polling switch. |
| [adu-broan-erv-uart-query.yaml](examples/adu-broan-erv-uart-query.yaml) | Shared-address queries with UART-controlled transmit direction. |
| [adu-broan-erv-recovery-query.yaml](examples/adu-broan-erv-recovery-query.yaml) | Shared-address recovery mode and bounded query trial buttons. |
| [adu-broan-erv-phase-query.yaml](examples/adu-broan-erv-phase-query.yaml) | Probes timed after selected VTTOUCH register groups. |

Query trial, polling, and phase buttons/switches in these examples use ESPHome template entities and C++ lambdas; they are not additional `platform: broan` settings. Enabling `experimental_query` permits probes; use the example controls to request them.

### Entity configuration

All entity keys below are optional. Add them under an entry with `platform: broan` in the indicated ESPHome domain. Each entry accepts `broan_id` to reference the parent component (automatically resolved when unambiguous). Each entity accepts its normal ESPHome options, such as `name`, `id`, `icon`, `internal`, and `disabled_by_default`; sensors also accept filters. For example:

```yaml
broan:
  id: erv
  uart_id: rs485
  passive_mode: true

sensor:
  - platform: broan
    broan_id: erv
    supply_fan_rpm:
      name: Supply Fan RPM
      id: supply_rpm
```

This passive snippet requires an RX-only `uart` configured separately. Telemetry is published when the ERV returns the corresponding register. A configured entity does not guarantee your model or existing controller supplies it. See [entities and freshness](docs/entities.md) for availability behavior and diagnostic template sensors.

#### Numeric sensors (`sensor`)

| Key | Unit | Meaning |
| --- | --- | --- |
| `power` | W | Instantaneous power reported by the ERV. |
| `temperature` | °C | Intake air temperature; may read high. |
| `temperature_out` | °C | Exhaust air temperature, if supported. |
| `filter_life` | d | Remaining filter life, converted from seconds to days by the component. Do not apply another seconds-to-days filter. |
| `supply_fan_cfm` | CFM | Reported supply airflow. |
| `exhaust_fan_cfm` | CFM | Reported exhaust airflow. |
| `supply_fan_rpm` | RPM | Supply fan motor speed. |
| `exhaust_fan_rpm` | RPM | Exhaust fan motor speed. |
| `humidity_target` | % | Observed target humidity; read-only counterpart to the setpoint control. |
| `intermittent_on_time` | min | Observed intermittent duration, converted from register seconds to minutes. |
| `bath_boost_remaining` | s | Reported bath boost countdown. Values above 86400 seconds are rejected. Becomes unknown after at most 10 seconds without a fresh sample; does not extrapolate a countdown between samples. |
| `turbo_remaining` | s | Reported turbo countdown, with the same validation and expiry as bath boost. |
| `fault_code` | None | Raw numeric fault code; `-1` means OK. |
| `warning_code` | None | Raw numeric warning code; `-1` means OK. |
| `base_mode_code` | None | Raw numeric configured/base mode code. |
| `active_mode_code` | None | Raw numeric current operating mode code. |

#### Text sensors (`text_sensor`)

| Key | Meaning |
| --- | --- |
| `fault_code` | Current fault as `OK` or `E<number>`. |
| `warning_code` | Current warning as `OK` or `W<number>`. |
| `last_fault` | Most recently observed non-OK fault with boot-relative observation time. Retained when the fault clears or goes stale; resets on reboot. |
| `active_mode` | Current operation as Idle, Low, High, Turbo, Medium, Bath Override, or a raw code. |
| `base_mode` | Configured/base mode as a raw code string. |
| `model` | ERV model identity string. |
| `firmware` | Firmware identity string. |
| `firmware_version` | Firmware version as dotted bytes. |
| `hardware_revision` | Hardware revision as dotted bytes. |
| `query_status` | Experimental query state, including wait, cooldown, recovery, and lockout information. |

The numeric and text `fault_code`/`warning_code` keys are separate entities under their respective domains and can be enabled together.

#### Binary sensors (`binary_sensor`)

| Key | Meaning |
| --- | --- |
| `fault_active` | True for a current non-OK fault, false for OK, unknown when fault telemetry is missing or stale in monitoring mode. |
| `fault_data_available` | True while a decoded fault register is younger than `sensor_timeout`; false before the first sample or after expiry. |
| `communication_ok` | True while any valid bus frame is younger than `communication_timeout`; starts false. Does not establish ERV health or individual sensor freshness. |
| `operating` | Reported operation: false for Idle (active code 0), true for known operating codes 1–5. Unknown before telemetry, after `sensor_timeout` in all modes, or for unrecognized codes. Supports HA history and runtime helpers; does not confirm physical fan movement. |
| `humidity_control_enabled` | Observed humidity-control enable state; unknown when missing or stale in monitoring mode. |

#### Controls (`select`, `number`, `switch`, `button`)

Use these for active controller replacement. Omit them on passive/query devices: their control methods are ignored in both modes.

| Domain | Key | Values and behavior |
| --- | --- | --- |
| `select` | `fan_mode` | Options: `off`, `min`, `max`, `int`, `manual`, `turbo`, `humidity`, `recirculate`, `smart`, `ovr`. Model support varies. The current setter falls back to `off` for `ovr`; use the dry-contact remote for override. |
| `number` | `fan_speed` | 0–100%, step 10. Sets manual/medium airflow between the configured min and max CFM; 50% is the midpoint. Requires valid min/max values from the ERV. |
| `number` | `humidity_setpoint` | 30–55%, step 5. Target for humidity control. Supply current humidity separately using `setCurrentHumidity()` as described above. |
| `number` | `intermittent_period` | 10–50000, step 1. Writes the intermittent duration register directly in seconds (the entity's default unit label is `Period`). For 20 minutes on per hour, use `1200`. The read-only `intermittent_on_time` sensor reports minutes. |
| `switch` | `humidity_control` | Enable or disable humidity control using the target and externally supplied current humidity. |
| `button` | `filter_reset` | Reset filter life to 7884000 seconds (about 3 months). |

### Active controller YAML example

Add this to an existing ESPHome device config for **controller replacement**. Adjust UART pins and add `broan.flow_control_pin` if your transceiver requires it. For observation alongside a VTTOUCH, use the passive example above instead.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/babgvant/broan_erv_uart
      ref: main
    components: [ broan ]

uart:
  id: rs485
  tx_pin: GPIO17 # Change these to match your rs485 tranceiver. 17/18 is txd1/rxd1
  rx_pin: GPIO18
  baud_rate: 38400
  rx_buffer_size: 2048
  #debug:
   # direction: BOTH
   # dummy_receiver: false
   # sequence:
   #   - lambda: UARTDebug::log_hex(direction, bytes, ' ');

broan:
  uart_id: rs485

select:
  - platform: broan
    fan_mode:
      name: "fan mode"

number:
  - platform: broan

    # Speed as a ratio of min vs max speed. Eg, if your MIN is 20 CFM and your MAX is 
    # 40 CFM, 50% means medium rill be at 30 CFM
    fan_speed:
      name: "fan speed"

    # Seconds ON per hour in int mode: 1200 means 20 minutes on, 40 minutes off.
    intermittent_period:
      name: "Intermittent Period"

sensor:
  - platform: broan
    # As reported by the ERV, in watts
    power:
      name: Power draw

    # Intake air temperature. This will generally read high
    temperature:
      name: Temperature

    # In Days
    filter_life:
      name: Remaining Filter life

    # CFM values as reported by the ERV
    supply_fan_cfm:
      name: "Supply fan CFM"
    exhaust_fan_cfm:
      name: "Exhaust fan CFM"

    # Fan motor RPM values
    supply_fan_rpm:
      name: "Supply fan RPM"
    exhaust_fan_rpm:
      name: "Exhaust fan RPM"

    # Not all ERVs have these sensor, remove if you don't get readings from it.

    # Exhaust air temperature.
    temperature_out:
      name: Temperature Out


text_sensor:
  - platform: broan
    
    # If there is a major fault, it will be indicated here. Else "OK"
    fault_code:
      name: "Fault code"
    
    # Same as above, but for warnings.
    warning_code:
      name: "Warning code"

    # Active mode. Indicates what the fans are currently doing
    active_mode:
      name: "Active Mode"

    # Potentially useful for development
    #model: { name: "Model" }
    #firmware: { name: "Firmware" }
    #firmware_version: { name: "Firmware Version" }
    #hardware_revision: { name: "Hardware Rev" }



button:
  - platform: broan

    # Resets filter life to 7884000 seconds / 3 months (Default behavior)
    filter_reset:
      name: Filter reset

```
