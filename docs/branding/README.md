# Broan ERV integration branding

The integration uses an original teal/amber airflow and heat-exchanger emblem. It represents supply and exhaust airflow, with sufficient color contrast for light and dark interfaces. It is project artwork, not the manufacturer's corporate wordmark.

The generated master is [icon-source.png](icon-source.png). The built-in image generation tool produced it with a transparent background. The export script only crops and downsamples this master, preserving its transparency and aspect ratio:

```text
python -m pip install Pillow
python scripts/export_branding.py
```

The shipped assets are `custom_components/broan_erv/brand/icon.png` (256×256) and `icon@2x.png` (512×512). No manifest change is needed. Home Assistant uses the same square emblem for logo requests through its supported icon fallback; separate duplicate logo files are unnecessary. Both themes use the same colored artwork. See the [Home Assistant branding requirements](https://developers.home-assistant.io/docs/core/integration/brand_images/).

## Generation prompt

Built-in image generation, transparent background enabled:

```text
Create one professional flat vector-style app icon for a community Home Assistant integration named Broan ERV, representing an energy recovery ventilator. No text or lettering at all. A compact square balanced emblem consisting of two broad, clean interlocking airflow arrows: teal blue supply air flowing toward the right through an abstract central diamond heat-exchange core, warm amber orange exhaust air flowing toward the left. The two paths are clearly separate and symmetric, conveying heat recovery and air circulation. Use bold simple geometric shapes with smooth precise edges, few details, strong recognizability at 32 pixels. Solid saturated teal #008FA3 and amber #E88A22, optionally very small negative-space details. The colored shapes must look legible on both a white and a dark charcoal UI background. Truly transparent background, no enclosing background square, no drop shadow, no gradient, no glow, no mockup, no border, no watermark, no corporate Broan wordmark or Home Assistant symbol. Center one single emblem, square overall shape, make it occupy nearly the entire canvas, minimal margins. Deliver a polished clean branding asset, 1024 by 1024.
```
