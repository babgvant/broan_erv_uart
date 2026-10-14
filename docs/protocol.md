# Broan passive monitoring protocol

Passive mode listens to the existing ERV and wall controller exchange. It does not register a controller, respond to flow control, acknowledge packets, poll, or send commands. Active mode remains the default.

## Repository architecture

| Responsibility | Existing implementation | Passive extension |
| --- | --- | --- |
| UART transport | `BroanComponent` extends `uart::UARTDevice` | Same RX transport; RX-only UART required by validation |
| Framing | `readHeader()` and `readMessage()` in `broan.cpp` | Shared bounded `PacketFramer` in `protocol.h`, replacing both methods |
| Checksum | `calculateChecksum()` | Same formula, shared `packet_checksum()` |
| Register definitions | `BroanField`, `BroanField_t`, `m_vecFields` in `broan.h` | Reused without a second register table |
| Conversion and publishing | `parseBroanFields()` | Reused with complete bounds and length validation |
| Registration and bus ownership | `handleMessage()`, `replyIfAllowed()` | Active branch retained; passive branch returns before these actions |
| Polling and heartbeat | `runTasks()` | Active only |
| Writes | `broan_control.cpp`, `writeRegisters()`, `queueMessage()`, `send()` | Every entry and internal transmit path guarded |
| Faults | `FaultCode`, `WarningCode`, text publishing | Reused formatting; retained last fault and fault binary sensor added |
| Freshness | Poll scheduling timestamp only | Independent receive timestamps in `broan_monitor.cpp` |

The smallest useful separation is between receive decoding and controller actions in `handleMessage()`. No second protocol decoder or request queue is needed for passive monitoring. The legacy `LISTEN_ONLY` compile-time switch is not the supported passive configuration.

## Packet format

Source of truth: the component's existing checksum, polling, write and register decoder code. The upstream author's [protocol notes](https://spitko.net/2025/08/08/Reverse-Engineering-an-ERV/) also describe self-contained register responses.

```text
01 TARGET SENDER 01 LENGTH PAYLOAD... CHECKSUM 04
```

`LENGTH` is the payload byte count, from 1 to 255. Frame size is `LENGTH + 7`. The checksum is `(1 - sum(header and payload)) & 0xFF`, excluding checksum and footer. This is an additive checksum, not Modbus CRC. The repository example uses 38400 baud; its validator specifies no parity and one stop bit. Eight data bits were the UART default. The validator now checks all four: **38400 8N1**. This is source-supported configuration; an actual installation still needs a capture to confirm timing and polarity.

The ERV address is `0x10`. Active integration address is `0x12`; other controller addresses are accepted for passive observation. The former arbitrary address ceiling of 32 is removed from framing. Passive telemetry is accepted only from sender `0x10`, regardless of destination. Controller writes do not update telemetry.

| Payload command | Structure | Passive action |
| --- | --- | --- |
| `02` / `03` | Ping / registration reply | Observe only |
| `04` / `05` | Bus ownership offer / acknowledgment | Observe only |
| `20` | Repeated two-byte register identifiers | Count requests for discovery |
| `21` | Repeated `[register byte 1, register byte 2, length, value...]` | Shared decoder publishes validated values |
| `40` | Write records with the same length/value structure | Observe only; commanded values are not measurements |
| `41` | Register identifiers acknowledging writes | Observe only |

Read responses are self-describing. They include register identifiers and lengths; they do not rely on `m_bExpectingReply` to identify their values. Consequently a response remains decodable after a missed request, and multiple controller destinations do not require a correlation table. Unsolicited `21` packets from the ERV are decoded the same way. Other unsolicited commands are logged when packet logging is enabled and have no inferred telemetry meaning.

The framer preserves partial packets, drains back-to-back packets, validates checksum and footer, and slides one byte after corruption. An incomplete candidate expires after a 250 ms gap; remaining complete candidates are recovered before discarding incomplete residue. At 38400 baud, a maximum frame occupies about 68 ms. A false length can delay decoding until this timeout but cannot hold the parser permanently. The framing buffer is bounded by the maximum frame size. Discovery retains at most 256 distinct IDs.

Each register response is validated in full before any entity updates. Numeric payloads must match the existing register type: byte length 1; integer and float length 4; void length 0. Version strings require at least three bytes. Unknown registers are skipped by their declared length. Wrong lengths or truncated records do not refresh values. Integer and IEEE-754 float bytes are copied in the original little-endian ESP32 representation.

## Register map

Identifiers below use the two bytes in **wire order**, not an assumed Modbus address encoding. These are the existing repository definitions, not a manufacturer protocol specification.

| Wire ID | Existing field | Type | Meaning |
| --- | --- | --- | --- |
| `0020` | FanMode | byte | Requested operating mode |
| `0F22` | HumidityControl | byte | Humidity control enabled |
| `0222` | IntModeDuration | uint32 | Intermittent on period, seconds |
| `0C22`, `0A22` | TargetHumidityA/B | float | Humidity targets |
| `1400` | Uptime | uint32 | Uptime, presumed seconds |
| `2350` | Wattage | float | Reported watts |
| `01E0`, `03E0` | TemperatureIn/Out | float | Celsius |
| `0510`, `0610` | SupplyCFM/ExhaustCFM | float | Reported CFM |
| `0310`, `0410` | SupplyRPM/ExhaustRPM | float | Measured motor RPM |
| `0622`, `0822` | CFMIn/Out_Medium | float | Medium flow targets |
| `0E50`, `0F50` | CFMIn/Out_Max | float | Maximum flow targets |
| `0A50`, `0B50` | CFMIn/Out_Min | float | Minimum flow targets |
| `0050` | Heartbeat | void | Controller keepalive write |
| `0450`, `0550` | ControllerHumidity/Temperature | float | Controller inputs |
| `0130` | FilterReset | byte | Reset command |
| `0830` | FilterLife | uint32 | Remaining seconds; published as days |
| `0930` | FilterLifeStage | uint32 | Staged reset interval |
| `1A00` | WarningCode | uint32 | `FFFFFFFF` means OK; warnings may cycle on reads |
| `1700` | FaultCode | uint32 | `FFFFFFFF` means OK; otherwise formatted `E<number>` |
| `0220`, `0720` | BaseMode/ActiveMode | uint32 | Requested and current state codes |
| `0200`, `0260` | Firmware/Model | string | Firmware identity and model |
| `0100`, `0160` | FirmwareVersion/HardwareRevision | string | First three bytes rendered as dotted numbers |

## Fault interpretation

A validated response containing `17 00 04 26 00 00 00` produces `E38`. This conversion is covered by synthetic tests. It does **not** establish that the VTTOUCH requests this register, that your ERV sends decimal 38 for its displayed E38, or that the fault survives a restart. Those require field evidence.

The existing active component polls both diagnostic registers every ten seconds. That schedule belongs to this integration, not to the VTTOUCH. Neither automatic fault transmission nor the VTTOUCH polling interval nor ERV-side fault retention is established by this repository. Capture a naturally occurring fault and its recovery to answer those questions. Do not induce a motor fault for testing.

`last_fault` records each newly observed non-OK code and a boot-relative observation time, retains it when the current code clears or goes stale, and resets on monitor reboot. The Home Assistant package preserves an absolute observation timestamp and a durable snapshot in helpers. Different faults update it; repeated unchanged faults do not rewrite the observation time. If a clearing response is missed, a recurrence of the same code may be indistinguishable from continuation of the original fault.
