"""Preview GBRS station HUD — fixed RDF-style geometry (mirrors GBRS_RadarStationHud.c)."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

STATION_MARGIN = 20
PANEL_GAP = 12
SIDE_W = 360
OPTICS_H = 280
AZEL_H = 468  # STATION_H - OPTICS_H - PANEL_GAP
LIST_W = 360
PPI_PANEL_W = 760
PPI_PANEL_H = 760
STATION_W = 1504
STATION_H = 760

INNER_PAD = 2.0
HEADER_H = 28.0

PPI_W = 640.0
PPI_H = 640.0
PPI_CX = 320.0
PPI_CY = 320.0
PPI_R = 300.0

AZEL_LABEL_L = 40.0
AZEL_LABEL_B = 22.0
AZEL_PAD_R = 10.0

OUT_DIR = Path(__file__).resolve().parent / "out"


def pin_root_center(screen_w: float, screen_h: float) -> tuple[float, float]:
    left = (screen_w - STATION_W) / 2.0
    top = (screen_h - STATION_H) / 2.0
    if left < STATION_MARGIN:
        left = float(STATION_MARGIN)
    if top < STATION_MARGIN:
        top = float(STATION_MARGIN)
    return left, top


def layout_station(ox: float, oy: float) -> dict:
    gap = float(PANEL_GAP)
    left_x = ox
    ppi_x = left_x + SIDE_W + gap
    list_x = ppi_x + PPI_PANEL_W + gap
    top_y = oy
    return {
        "root": (ox, oy, float(STATION_W), float(STATION_H)),
        "optics": (left_x, top_y, float(SIDE_W), float(OPTICS_H)),
        "azel": (left_x, top_y + OPTICS_H + gap, float(SIDE_W), float(AZEL_H)),
        "ppi": (ppi_x, top_y, float(PPI_PANEL_W), float(PPI_PANEL_H)),
        "list": (list_x, top_y, float(LIST_W), float(PPI_PANEL_H)),
    }


def layout_azel_plot(panel: tuple[float, float, float, float]) -> dict:
    px, py, pw, ph = panel
    iw = pw - INNER_PAD * 2.0
    ih = ph - INNER_PAD * 2.0
    plot_x = AZEL_LABEL_L
    plot_y = HEADER_H + 4.0
    plot_w = iw - plot_x - AZEL_PAD_R
    plot_h = ih - plot_y - AZEL_LABEL_B
    return {
        "inner": (px + INNER_PAD, py + INNER_PAD, iw, ih),
        "plot": (px + INNER_PAD + plot_x, py + INNER_PAD + plot_y, plot_w, plot_h),
        "inner_w": iw,
    }


def layout_ppi_canvas() -> dict:
    iw = PPI_PANEL_W - INNER_PAD * 2.0
    ih = PPI_PANEL_H - INNER_PAD * 2.0
    top_reserve = HEADER_H + 4.0
    bottom_reserve = 24.0
    avail_h = ih - top_reserve - bottom_reserve
    canvas_x = float(int((iw - PPI_W) * 0.5))
    canvas_y = top_reserve + float(int((avail_h - PPI_H) * 0.5))
    return {
        "inner": (INNER_PAD, INNER_PAD, iw, ih),
        "canvas": (canvas_x, canvas_y, PPI_W, PPI_H),
        "cx": canvas_x + PPI_CX,
        "cy": canvas_y + PPI_CY,
        "r": PPI_R,
        "panel": (float(PPI_PANEL_W), float(PPI_PANEL_H)),
    }


def draw_ppi_disc(draw: ImageDraw.ImageDraw, ox: float, oy: float, geo: dict) -> None:
    cx = ox + geo["cx"]
    cy = oy + geo["cy"]
    r = geo["r"]
    draw.ellipse(
        [cx - r, cy - r, cx + r, cy + r],
        fill=(2, 14, 8, 255),
        outline=(60, 230, 140, 200),
        width=2,
    )
    for frac in (0.25, 0.5, 0.75):
        rr = r * frac
        draw.ellipse(
            [cx - rr, cy - rr, cx + rr, cy + rr],
            outline=(40, 160, 100, 120),
            width=1,
        )
    draw.line([cx, cy - r + 6, cx, cy + r - 6], fill=(50, 180, 120, 140), width=1)
    draw.line([cx - r + 6, cy, cx + r - 6, cy], fill=(50, 180, 120, 140), width=1)

    bearing = math.radians(40.0)
    half = math.radians(8.0)
    pts = [(cx, cy)]
    for i in range(17):
        a = bearing - half + (2 * half) * (i / 16.0)
        pts.append((cx + math.sin(a) * r, cy - math.cos(a) * r))
    draw.polygon(pts, fill=(50, 210, 120, 70))
    draw.line(
        [cx, cy, cx + math.sin(bearing) * r, cy - math.cos(bearing) * r],
        fill=(130, 255, 180, 240),
        width=3,
    )


def draw_azel_labels(draw: ImageDraw.ImageDraw, panel, font) -> None:
    geo = layout_azel_plot(panel)
    plot_x, plot_y, plot_w, plot_h = geo["plot"]
    inner_x = geo["inner"][0]
    inner_w = geo["inner_w"]
    draw.rectangle(
        [plot_x, plot_y, plot_x + plot_w, plot_y + plot_h],
        fill=(4, 10, 16, 255),
        outline=(100, 190, 220, 180),
        width=1,
    )
    for i in range(7):
        x = plot_x + plot_w * (i / 6.0)
        draw.line([x, plot_y, x, plot_y + plot_h], fill=(70, 140, 180, 90), width=1)
    for j in range(5):
        y = plot_y + plot_h * (j / 4.0)
        draw.line([plot_x, y, plot_x + plot_w, y], fill=(70, 140, 180, 90), width=1)

    el_right = plot_x - 6.0
    for j, el in enumerate((55, 40, 25, 10, -5)):
        y = plot_y + plot_h * (j / 4.0)
        text = str(el)
        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        draw.text((el_right - tw, y - 6), text, fill=(190, 235, 255, 255), font=font)

    az_y = plot_y + plot_h + 6.0
    az_half_w = 18.0
    for i, az in enumerate((0, 60, 120, 180, 240, 300, 360)):
        # Local X inside AzElInner, matching GBRS_RadarStationHud.BuildAzElAxisLabels.
        local_tick = AZEL_LABEL_L + plot_w * (i / 6.0)
        local_x = local_tick
        if local_x < az_half_w + 4.0:
            local_x = az_half_w + 4.0
        if local_x > inner_w - az_half_w - 4.0:
            local_x = inner_w - az_half_w - 4.0
        abs_x = inner_x + local_x
        text = str(az)
        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        draw.text((abs_x - tw * 0.5, az_y), text, fill=(190, 235, 255, 255), font=font)


def render(screen_w: float, screen_h: float, label: str) -> Path:
    left, top = pin_root_center(screen_w, screen_h)
    st = layout_station(left, top)
    img = Image.new("RGBA", (int(screen_w), int(screen_h)), (18, 22, 28, 255))
    draw = ImageDraw.Draw(img, "RGBA")
    try:
        font = ImageFont.truetype("arial.ttf", 14)
        font_sm = ImageFont.truetype("arial.ttf", 11)
    except OSError:
        font = ImageFont.load_default()
        font_sm = font

    def panel(rect, color, title: str) -> None:
        x, y, w, h = rect
        draw.rectangle([x, y, x + w, y + h], fill=color, outline=(120, 200, 255, 180), width=2)
        draw.text((x + 10, y + 6), title, fill=(200, 230, 255, 255), font=font)

    rx, ry, rw, rh = st["root"]
    draw.rectangle([rx, ry, rx + rw, ry + rh], outline=(255, 255, 0, 90), width=1)

    panel(st["optics"], (20, 50, 80, 200), "OPTICAL SIGHT")
    panel(st["azel"], (20, 50, 80, 200), "ELEVATION-AZIMUTH")
    draw_azel_labels(draw, st["azel"], font_sm)
    panel(st["list"], (25, 45, 75, 200), "TRACKED CONTACTS")

    px, py, pw, ph = st["ppi"]
    draw.rectangle([px, py, px + pw, py + ph], fill=(8, 40, 22, 220), outline=(80, 220, 140, 200), width=2)
    draw.text((px + 14, py + 8), "PLAN POSITION INDICATOR", fill=(120, 255, 160, 255), font=font)
    draw.text((px + pw - 200, py + 8), "PD SEARCH   7.0km", fill=(100, 220, 140, 255), font=font_sm)

    geo = layout_ppi_canvas()
    ix, iy, iw, ih = geo["inner"]
    draw.rectangle(
        [px + ix, py + iy, px + ix + iw, py + iy + ih],
        outline=(60, 180, 100, 100),
        width=1,
    )
    cx, cy, cw, ch = geo["canvas"]
    draw.rectangle(
        [
            px + INNER_PAD + cx,
            py + INNER_PAD + cy,
            px + INNER_PAD + cx + cw,
            py + INNER_PAD + cy + ch,
        ],
        outline=(255, 220, 80, 160),
        width=1,
    )
    draw_ppi_disc(draw, px + INNER_PAD, py + INNER_PAD, geo)

    draw.line([screen_w * 0.5, 0, screen_w * 0.5, screen_h], fill=(255, 255, 0, 60), width=1)
    draw.line([0, screen_h * 0.5, screen_w, screen_h * 0.5], fill=(255, 255, 0, 60), width=1)

    circle_bottom = py + INNER_PAD + geo["cy"] + geo["r"]
    panel_bottom = py + ph
    clipped = circle_bottom > panel_bottom - 1.0
    status = "CLIPPED" if clipped else "OK full circle"
    draw.text(
        (20, screen_h - 36),
        f"{label}  {int(screen_w)}x{int(screen_h)}  station={STATION_W}x{STATION_H} "
        f"ppi={PPI_W:.0f} r={PPI_R:.0f}  {status}",
        fill=(255, 80, 80, 255) if clipped else (120, 255, 160, 255),
        font=font,
    )

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / f"hud_preview_{label}.png"
    img.convert("RGB").save(path)
    print(path)
    print(
        f"  pin=({left:.1f},{top:.1f}) ppi_panel=({pw:.1f}x{ph:.1f}) "
        f"canvas={geo['canvas'][2]:.1f} r={geo['r']:.1f} "
        f"circle_bottom={circle_bottom:.1f} panel_bottom={panel_bottom:.1f} clipped={clipped}"
    )
    return path


def main() -> None:
    render(2369.36, 1080.0, "ref_unscale")
    render(2071.0, 944.0, "raw_ws")
    render(1920.0, 1080.0, "fhd")


if __name__ == "__main__":
    main()
