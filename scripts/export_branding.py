"""Export the generated branding master at Home Assistant's icon sizes."""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs/branding/icon-source.png"
DESTINATION = ROOT / "custom_components/broan_erv/brand"


def main():
    image = Image.open(SOURCE).convert("RGBA")
    bounds = image.getchannel("A").point(lambda alpha: 255 if alpha >= 128 else 0).getbbox()
    if bounds is None:
        raise ValueError("The branding source is empty")
    left, top, right, bottom = bounds
    side = max(right - left, bottom - top)
    x = max(0, min(image.width - side, (left + right - side) // 2))
    y = max(0, min(image.height - side, (top + bottom - side) // 2))
    square = image.crop((x, y, x + side, y + side))
    DESTINATION.mkdir(parents=True, exist_ok=True)
    for filename, size in (("icon.png", 256), ("icon@2x.png", 512)):
        output = DESTINATION / filename
        square.resize((size, size), Image.Resampling.LANCZOS).save(output, optimize=True)
        print(f"{output.relative_to(ROOT)}: {size}x{size} RGBA")


if __name__ == "__main__":
    main()
