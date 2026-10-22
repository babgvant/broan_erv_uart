# ERV operation history in Home Assistant

Home Assistant can show that the ERV is idle now and was active earlier today. The existing Active Mode entity provides a mode timeline; the optional Operating binary sensor adds an active/idle timeline and a source for runtime totals. History is stored by Home Assistant, so monitor reboots do not erase already recorded history.

For UI-based import and entity management, install the [HACS companion](hacs.md). The examples below also work directly with ESPHome entities; when using the companion, replace the example entity IDs with its imported sensor IDs.

## Enable the telemetry

Update the external component, add these keys to your existing ESPHome `platform: broan` entries, and install the updated firmware. Merge with existing domain blocks rather than creating duplicate YAML keys.

```yaml
binary_sensor:
  - platform: broan
    operating:
      name: Operating

text_sensor:
  - platform: broan
    active_mode:
      name: Active Mode
    base_mode:
      name: Base Mode
```

The passive and query examples already include these entities. For a Git external source, refresh its cached component before compiling if ESPHome still uses an older checkout. In Home Assistant, find the actual IDs under Settings > Devices & services > ESPHome > your device. The examples below assume `binary_sensor.adu_erv_operating`, `sensor.adu_erv_active_mode`, and `sensor.adu_erv_base_mode`; names and existing registry entries can produce different IDs.

Operating is off for Idle (code 0), and on for Low, High, Turbo, Medium, or Bath Override (codes 1–5). It stays unknown until a valid active-mode response arrives and becomes unknown when that register expires (`sensor_timeout`, default 300 seconds), or if an unrecognized mode code arrives. API disconnection makes it unavailable. These states must not be converted to off. Operation describes the ERV's reported mode; use RPM/CFM history alongside it to check physical fan behavior.

The integration observes transitions when telemetry arrives. Passive monitoring cannot detect transitions the controller never reports, and a last known state can persist up to the timeout before becoming unknown. Do not increase the timeout just to fill gaps in a graph.

## View earlier activity

Open Home Assistant's History panel, select Active Mode and Operating, and choose today's date/time range. If Active Mode was already recorded, you can inspect its earlier history immediately, before updating firmware. Base Mode shows the configured program (such as Intermittent), while Active Mode shows the reported operation within that program.

For a dashboard, edit the dashboard, add a Manual card, and paste [home-assistant-history-card.yaml](../examples/home-assistant-history-card.yaml), replacing entity IDs. The card shows a rolling 24-hour timeline and the runtime helpers below. Remove the entities card section if you only want the timeline. See the official [History graph card documentation](https://www.home-assistant.io/dashboards/history-graph/).

## Add runtime totals

Copy [home-assistant-history-package.yaml](../examples/home-assistant-history-package.yaml) into your HA configuration directory. It creates operating hours since local midnight and over the last 24 hours. Merge this into the existing `homeassistant` configuration:

```yaml
homeassistant:
  packages:
    adu_erv_history: !include home-assistant-history-package.yaml
```

Replace the source entity ID in both helpers, check the Home Assistant configuration, and restart HA. The history package can coexist with the alerts package. Alternatively, create a History Stats helper under Settings > Devices & services > Helpers, select Operating, match state `on`, and choose type Time. For today, use start `{{ today_at() }}` and end `{{ now() }}`. The [History Stats documentation](https://www.home-assistant.io/integrations/history_stats/) describes the options. Time is reported in hours, so 0.5 means 30 minutes.

Only recorded `on` intervals contribute to runtime; unknown and unavailable intervals are not counted as operation. A partial day's data is a partial total, not proof the ERV was idle during missing periods. HA cannot reconstruct activity while it was offline or before the entity was recorded. A new Operating entity cannot backfill itself from old Active Mode records.

## Retain enough history

[Recorder](https://www.home-assistant.io/integrations/recorder/) normally retains detailed history for 10 days. Make sure the source entities are not excluded. If you use a Recorder include allowlist, add Operating, Active Mode, Base Mode, and the runtime sensors to that existing list. Do not introduce a new include list just for this feature: it would restrict recording for your other entities.

For 30 days of detailed timelines, merge into your existing Recorder block:

```yaml
recorder:
  purge_keep_days: 30
```

Longer retention increases database storage. Raw mode and binary timelines are limited by Recorder retention; numeric long-term statistics do not preserve individual mode transitions. Keep history at least as long as the History Stats window you query. Increasing retention cannot recover records already purged.
