"""Generate the fixed 640x640 PPI face used by RadarStationHUD.layout."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw


SIZE = 640
SCALE = 4
CENTER = 320
RADIUS = 300


def scaled(value: float) -> int:
    return round(value * SCALE)


def ellipse_box(radius: float) -> tuple[int, int, int, int]:
    center = scaled(CENTER)
    extent = scaled(radius)
    return (
        center - extent,
        center - extent,
        center + extent,
        center + extent,
    )


def main() -> None:
    image = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image, "RGBA")

    draw.ellipse(ellipse_box(RADIUS), fill=(2, 14, 8, 255))
    draw.ellipse(ellipse_box(RADIUS * 0.92), fill=(4, 22, 12, 255))

    for fraction in (0.25, 0.5, 0.75):
        draw.ellipse(
            ellipse_box(RADIUS * fraction),
            outline=(40, 160, 100, 110),
            width=scaled(1.5),
        )

    draw.ellipse(
        ellipse_box(RADIUS),
        outline=(60, 230, 140, 180),
        width=scaled(2.0),
    )

    center = scaled(CENTER)
    inner = scaled(CENTER - RADIUS + 6)
    outer = scaled(CENTER + RADIUS - 6)
    draw.line((center, inner, center, outer), fill=(50, 180, 120, 80), width=scaled(1.5))
    draw.line((inner, center, outer, center), fill=(50, 180, 120, 80), width=scaled(1.5))

    for index in range(12):
        bearing = math.radians(index * 30.0)
        tick_length = 14.0 if index % 3 == 0 else 8.0
        outer_radius = RADIUS - 2.0
        inner_radius = outer_radius - tick_length
        sin_bearing = math.sin(bearing)
        cos_bearing = math.cos(bearing)
        draw.line(
            (
                scaled(CENTER + sin_bearing * inner_radius),
                scaled(CENTER - cos_bearing * inner_radius),
                scaled(CENTER + sin_bearing * outer_radius),
                scaled(CENTER - cos_bearing * outer_radius),
            ),
            fill=(80, 245, 160, 190),
            width=scaled(2.0),
        )

    draw.ellipse(ellipse_box(4.0), fill=(70, 255, 160, 255))

    image = image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    output = (
        Path(__file__).resolve().parents[3]
        / "UI"
        / "Textures"
        / "GBRS"
        / "GBRS_PpiFace.png"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)
    print(output)


if __name__ == "__main__":
    main()
