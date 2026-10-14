# Waveshare passive receiver wiring

The ADU example requires a physical receive-only barrier before connection to the live ERV bus. GPIO settings alone cannot guarantee silence during boot, reset, crashes, flashing, or alternate firmware.

## Schematic findings

The published [ESP32-S3-RS485-CAN schematic](https://files.waveshare.com/wiki/ESP32-S3-RS485-CAN/ESP32-S3-RS485-CAN-Schematic.pdf) shows U7 SP3485EN. GPIO17 is TX, GPIO18 RX, and GPIO21 `RS485_EN`, passed through U9 isolation. U7 pins 2 (`/RE`) and 3 (DE) share `RS485_EN'`; R22 is a 4.7 kΩ pull-up. There is no independent GPIO for disabling the transmitter. The diagram uses explicit direction control, not a TX-driven automatic direction circuit. R24/R27 provide 4.7 kΩ bus bias; H2 switches R25, the 120 Ω termination. J6 labels A/B. The schematic is for the family board; confirm the purchased U revision matches it. [Schematic](https://files.waveshare.com/wiki/ESP32-S3-RS485-CAN/ESP32-S3-RS485-CAN-Schematic.pdf)

With a matching revision, GPIO21 low selects receive after software initializes. The pull-up and isolator behavior prevent treating that as a boot-time hardware guarantee. Enabling UART automatic RTS direction control would still let software assert the driver.

## Receive only modification

A technician can isolate **both U7 pins 2 and 3** from `RS485_EN'` and R22, then connect them permanently to **SGND**, the isolated transceiver ground at U7 pin 5. Separating the local pins from the existing node avoids shorting U9's output or the pull-up to ground. Lifting the pins and wiring them low is one possible implementation; the actual board must determine whether a clean trace cut is practical. Verify continuity before applying power. Do not tie the intact driven node low without isolating it.

DE low disables the driver; `/RE` low enables the receiver, according to the [SP3485 datasheet](https://www.maxlinear.com/ds/sp3485.pdf). TX activity then cannot intentionally drive A/B. This barrier depends on correct rework and intact hardware, and is not a guarantee against a shorted transceiver or cable. For easy replacement, use a separate isolated 3.3 V-compatible RS485 receiver with DE permanently low and `/RE` permanently low; connect only its RO output to an accessible ESP32 RX GPIO and leave the onboard RS485 terminals unused. Update `rx_pin` accordingly.

Remove H2 termination and, for a tap intended to preserve existing bias, remove or disconnect R24 and R27 after confirming their identities on the actual PCB. Keep the ERV bus's original termination and bias. An extra 120 Ω resistor can substantially increase loading. A receiver-only board without added bias avoids this rework.

## Parallel tap

After the bench procedure in [validation.md](validation.md), connect a short twisted-pair stub near the ERV terminal block. Leave the VTTOUCH wiring and both VBATHW connections in place. Do not route the existing controller through the monitoring device.

| Existing ERV connection | Monitor connection |
| --- | --- |
| D+ | Matching positive receiver input, nominally Waveshare A+ |
| D− | Matching negative receiver input, nominally Waveshare B− |
| Bus reference | Receiver-side isolated reference, if provided and confirmed by continuity |
| Existing controller power and auxiliary wiring | Leave in place |

A/B conventions vary. Use the actual terminal labels and bench polarity check; do not apply the original README's generic A/B reversal automatically. Wrong polarity produces undecodable traffic. The two-pin J6 in the schematic does not establish a third bus-reference terminal. Identify any exposed SGND reference before using it; logic GND and isolated SGND are different nets. Avoid bypassing isolation with a guessed ground connection.

Use an independent supply at the board's permitted input so monitor faults do not load the ERV's controller supply. The manufacturer's [wiki](https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN) specifies 7–36 V at the DC input and 5 V USB power, warns against using both simultaneously, and describes antenna selection by moving a resistor. Confirm the U board's antenna routing before attaching an external antenna.

An unpowered receiver can still load the bus through protection or bias circuitry. The powered-off bench test is required. Parallel wiring ensures that disconnecting the monitor leaves the original path intact; no parallel electronic tap can promise immunity to every electrical short.
