# Broan ERV in HACS

This repository contains two parts:

- `components/broan`: the ESPHome firmware component that reads the ERV's RS485 bus.
- `custom_components/broan_erv`: the Home Assistant integration installed by HACS. It imports selected existing ESPHome telemetry into a Broan ERV device and provides a reported-operation binary sensor for history.

The ESP32 and Home Assistant's ESPHome integration remain the transport. HACS updates the HA integration; updating the firmware still requires an ESPHome build/install. The HA integration does not open another connection to the ESP32 or transmit UART commands. It uses local HA state-change events.

## Installation

Requires Home Assistant 2026.10 or later and an ESPHome ERV monitor already added through Settings > Devices & services > ESPHome. Enable the source telemetry entities you want to import. At minimum, expose Active Mode as a text sensor or numeric code sensor. For wiring and operating mode, follow the existing [firmware instructions](../README.md#passive-monitoring).

Once these changes are available in the GitHub repository:

1. Open HACS, select its menu, then **Custom repositories**.
2. Add `https://github.com/babgvant/broan_erv_uart` with category **Integration**.
3. Find **Broan ERV** in HACS and download it. Restart Home Assistant.
4. Go to Settings > Devices & services > **Add integration**, and search for **Broan ERV**.
5. Give the ERV a name and select the ESPHome monitor device.
6. Select the Active Mode source. Review suggested matches for optional telemetry, clear any you do not want, and submit.

If a release exists that predates the HA integration, select the updated default branch in HACS or a release containing `custom_components/broan_erv`. A release tag alone is not a HACS release; a GitHub release must be published to offer a numbered version.

For manual installation before publication, copy the complete `custom_components/broan_erv` directory into `<HA config>/custom_components/broan_erv`, restart HA, and continue at step 4. Installing the ESPHome `components/broan` directory there will not work.

The repository is intended for addition as a HACS **custom repository**. Inclusion in the default HACS catalog is a separate publication step. Branding is bundled in `custom_components/broan_erv/brand/`; current Home Assistant versions load these local images without a separate Brands repository submission, and HACS validation checks for the bundled icon. The public repository needs to contain these changes before a HACS download can include them. See [HACS custom repositories](https://www.hacs.dev/docs/faq/custom_repositories/), [integration packaging requirements](https://www.hacs.dev/docs/publish/integration/), and [local branding](https://developers.home-assistant.io/docs/core/integration/brand_images/).

## Managing sensors

Broan ERV appears as its own integration with a device and only the sensors you selected. You can rename, hide, disable, and assign the imported entities to areas through Home Assistant's usual entity/device controls. To change the source mappings or add/remove imported sensors, open the integration entry's menu and choose **Reconfigure**. One entry is allowed per ESPHome device; multiple monitor devices can be added separately.

Mappings store the source's entity-registry ID, so renaming an ESPHome source entity does not change the imported entity's identity. Deleting and recreating the source can give it a new registry ID: reconfigure the mapping afterward. If you replace the entire ESPHome device, add its replacement as a new integration entry.

The source entities still belong to ESPHome, and the imported entities belong to Broan ERV. Keep the source entities **enabled**. You can hide them from dashboards or exclude them from Recorder after switching automations/history helpers to the imported entities. Hiding is different from disabling: disabled source entities stop providing updates.

Only sensors and binary sensors are imported. Existing fan controls, numbers, switches, and buttons stay under ESPHome. Adding this companion does not change passive/active UART behavior.

## Operation and history

Operating uses the ESPHome Operating binary sensor if you select it. This is recommended with the updated firmware because that sensor expires its active-mode data in all firmware modes. Otherwise the integration derives operation from Active Mode: Idle/code 0 is off; Low, High, Turbo, Medium, Bath Override/codes 1–5 are on. An unrecognized mode is unknown. Missing, unknown, literal `Unknown`, and unavailable source states produce unavailable imported entities. Numeric sensors reject non-finite or malformed values instead of treating them as zero.

The companion relies on the firmware's freshness handling; it does not invent a new timeout from the last HA state change, since an unchanged mode can be valid for hours. With older active-controller firmware, prefer updating and selecting the native Operating source to get per-register expiry. Telemetry still depends on what your ERV and controller report.

After setup, open an imported entity's History view to see transitions. Use [the history guide](history.md) and its runtime helper/dashboard examples, replacing the example IDs with your **Broan ERV** entity IDs. For example, an integration named Broan ERV will generally create `binary_sensor.broan_erv_operating` and `sensor.broan_erv_active_mode`. Verify them in your entity registry, since naming settings and existing IDs affect the result.

New imported entities start new history. Existing ESPHome history remains associated with its original entity IDs; installing HACS does not migrate or backfill it. Keep the imported Operating entity in Recorder for runtime calculations. Operation represents the reported mode, not measured proof that the fans are turning; import RPM and CFM for that comparison.

## Updates and removal

Use HACS to download integration updates, then restart HA when prompted. This does not update or reflash the ESP32. Remove the Broan ERV entry through Devices & services before uninstalling its HACS download. Removing the companion leaves the ESPHome device and firmware configuration in place.

## Development checks

Portable decoding/metadata checks are part of the existing `tests` suite. `tests_ha` exercises UI setup, duplicate detection, state propagation, unknown/unavailable handling, source rename/removal, reconfiguration, and unloading against HA Core. Run those separately on a supported Linux Python 3.14 environment:

```text
python -m pip install -r tests_ha/requirements.txt
python -m pytest -q tests_ha --asyncio-mode=auto
```

The Home Assistant workflow runs these tests and HACS repository validation on push/PR. These checks do not establish physical UART behavior or install anything on a user's HA instance.
