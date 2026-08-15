#!/usr/bin/env python3
"""Convert a photograph to Waveshare 7.3-inch E (Spectra 6) packed data."""

from __future__ import annotations

import argparse
import colorsys
import math
from pathlib import Path

from PIL import Image, ImageEnhance, ImageFilter

WIDTH, HEIGHT = 800, 480
CONTRAST, SATURATION, SHARPNESS, BLUR_RADIUS = 1.04, 1.08, 1.04, 0.05
DITHER_STRENGTH, MAX_ERROR = 0.92, 72.0
EDGE_DITHER_MIN = 0.72
EDGE_THRESHOLD_LOW, EDGE_THRESHOLD_HIGH, EDGE_BLUR_RADIUS = 20.0, 90.0, 0.6
SKIN_BIAS_STRENGTH = 0.22
WHITE_PENALTY, BLACK_PENALTY = 1.035, 1.015

PALETTE = [
    {"name": "BLACK", "code": 0x0, "rgb": (15, 15, 15)},
    {"name": "WHITE", "code": 0x1, "rgb": (245, 242, 232)},
    {"name": "YELLOW", "code": 0x2, "rgb": (235, 195, 30)},
    {"name": "RED", "code": 0x3, "rgb": (200, 45, 38)},
    {"name": "BLUE", "code": 0x5, "rgb": (38, 75, 160)},
    {"name": "GREEN", "code": 0x6, "rgb": (55, 135, 70)},
]


def clamp(v: float, lo: float = 0.0, hi: float = 255.0) -> float:
    return max(lo, min(hi, v))


def srgb_to_linear(v: float) -> float:
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def rgb_to_lab(rgb: tuple[float, float, float]) -> tuple[float, float, float]:
    r, g, b = (srgb_to_linear(v) for v in rgb)
    x = (r * .4124564 + g * .3575761 + b * .1804375) * 100 / 95.047
    y = (r * .2126729 + g * .7151522 + b * .0721750) * 100 / 100.0
    z = (r * .0193339 + g * .1191920 + b * .9503041) * 100 / 108.883
    delta = 6 / 29

    def f(t: float) -> float:
        return t ** (1 / 3) if t > delta**3 else t / (3 * delta**2) + 4 / 29

    fx, fy, fz = f(x), f(y), f(z)
    return 116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)


def ciede2000(lab1, lab2) -> float:
    l1, a1, b1 = lab1
    l2, a2, b2 = lab2
    c1, c2 = math.hypot(a1, b1), math.hypot(a2, b2)
    avg_c = (c1 + c2) / 2
    g = .5 * (1 - math.sqrt(avg_c**7 / (avg_c**7 + 25**7)))
    a1p, a2p = (1 + g) * a1, (1 + g) * a2
    c1p, c2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360
    h2p = math.degrees(math.atan2(b2, a2p)) % 360
    dlp, dcp = l2 - l1, c2p - c1p
    dhp = h2p - h1p
    if c1p * c2p == 0:
        dhp = 0
    elif dhp > 180:
        dhp -= 360
    elif dhp < -180:
        dhp += 360
    dh_term = 2 * math.sqrt(c1p * c2p) * math.sin(math.radians(dhp / 2))
    avg_lp, avg_cp = (l1 + l2) / 2, (c1p + c2p) / 2
    if c1p * c2p == 0:
        avg_hp = h1p + h2p
    elif abs(h1p - h2p) <= 180:
        avg_hp = (h1p + h2p) / 2
    elif h1p + h2p < 360:
        avg_hp = (h1p + h2p + 360) / 2
    else:
        avg_hp = (h1p + h2p - 360) / 2
    t = (1 - .17 * math.cos(math.radians(avg_hp - 30))
         + .24 * math.cos(math.radians(2 * avg_hp))
         + .32 * math.cos(math.radians(3 * avg_hp + 6))
         - .20 * math.cos(math.radians(4 * avg_hp - 63)))
    dt = 30 * math.exp(-((avg_hp - 275) / 25) ** 2)
    rc = 2 * math.sqrt(avg_cp**7 / (avg_cp**7 + 25**7))
    sl = 1 + .015 * (avg_lp - 50) ** 2 / math.sqrt(20 + (avg_lp - 50) ** 2)
    sc, sh = 1 + .045 * avg_cp, 1 + .015 * avg_cp * t
    rt = -math.sin(math.radians(2 * dt)) * rc
    tl, tc, th = dlp / sl, dcp / sc, dh_term / sh
    return math.sqrt(tl * tl + tc * tc + th * th + rt * tc * th)


for color in PALETTE:
    color["lab"] = rgb_to_lab(color["rgb"])
PALETTE_BY_CODE = {c["code"]: c for c in PALETTE}


def resize_and_crop(img: Image.Image) -> Image.Image:
    scale = max(WIDTH / img.width, HEIGHT / img.height)
    size = round(img.width * scale), round(img.height * scale)
    img = img.resize(size, Image.Resampling.LANCZOS)
    left, top = (img.width - WIDTH) // 2, (img.height - HEIGHT) // 2
    return img.crop((left, top, left + WIDTH, top + HEIGHT))


def preprocess(img: Image.Image) -> Image.Image:
    if BLUR_RADIUS:
        img = img.filter(ImageFilter.GaussianBlur(BLUR_RADIUS))
    img = ImageEnhance.Contrast(img).enhance(CONTRAST)
    img = ImageEnhance.Color(img).enhance(SATURATION)
    return ImageEnhance.Sharpness(img).enhance(SHARPNESS)


def skin_probability(rgb) -> float:
    r, g, b = rgb
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    hue = h * 360
    hue_distance = min(abs(hue - 360), abs(hue)) if hue > 180 else abs(hue - 25)
    hs = clamp(1 - hue_distance / 55, 0, 1)
    ss, vs = clamp((s - .05) / .55, 0, 1), clamp((v - .18) / .55, 0, 1)
    cb = 128 - .168736 * r - .331264 * g + .5 * b
    cr = 128 + .5 * r - .418688 * g - .081312 * b
    return clamp(hs * ss * vs * clamp(1 - abs(cb - 103) / 45, 0, 1)
                 * clamp(1 - abs(cr - 153) / 45, 0, 1), 0, 1)


def color_penalty(rgb, code: int) -> float:
    brightness = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2]
    p = WHITE_PENALTY * (1.035 if brightness < 175 else 1) if code == 1 else 1.0
    if code == 0:
        p *= BLACK_PENALTY * (1.025 if brightness > 120 else 1)
    amount = skin_probability(rgb) * SKIN_BIAS_STRENGTH
    if code == 2:
        p *= 1 - .18 * amount
    elif code == 3:
        p *= 1 - .12 * amount
    elif code == 5:
        p *= 1 + .18 * amount
    elif code == 6:
        p *= 1 + .14 * amount
    return p


def nearest_color(rgb):
    lab = rgb_to_lab(rgb)
    return min(PALETTE, key=lambda c: ciede2000(lab, c["lab"]) * color_penalty(rgb, c["code"]))


def create_edge_map(img: Image.Image) -> Image.Image:
    src = img.convert("L").load()
    edge = Image.new("L", (WIDTH, HEIGHT), 0)
    dst = edge.load()
    for y in range(1, HEIGHT - 1):
        for x in range(1, WIDTH - 1):
            gx = (-src[x-1,y-1] + src[x+1,y-1] - 2*src[x-1,y] + 2*src[x+1,y]
                  - src[x-1,y+1] + src[x+1,y+1])
            gy = (-src[x-1,y-1] - 2*src[x,y-1] - src[x+1,y-1]
                  + src[x-1,y+1] + 2*src[x,y+1] + src[x+1,y+1])
            dst[x, y] = min(255, int(math.hypot(gx, gy)))
    return edge.filter(ImageFilter.GaussianBlur(EDGE_BLUR_RADIUS))


def local_strength(edge: int) -> float:
    if edge <= EDGE_THRESHOLD_LOW:
        return DITHER_STRENGTH
    if edge >= EDGE_THRESHOLD_HIGH:
        return DITHER_STRENGTH * EDGE_DITHER_MIN
    t = (edge - EDGE_THRESHOLD_LOW) / (EDGE_THRESHOLD_HIGH - EDGE_THRESHOLD_LOW)
    return DITHER_STRENGTH * (1 + (EDGE_DITHER_MIN - 1) * t)


def add_error(work, x, y, er, eg, eb, weight):
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        work[y][x][0] += er * weight
        work[y][x][1] += eg * weight
        work[y][x][2] += eb * weight


def stucki_dither(img: Image.Image, edge_map: Image.Image):
    source, edge = img.load(), edge_map.load()
    work = [[[float(v) for v in source[x, y]] for x in range(WIDTH)] for y in range(HEIGHT)]
    codes = [[0] * WIDTH for _ in range(HEIGHT)]
    kernel = [(1,0,8), (2,0,4), (-2,1,2), (-1,1,4), (0,1,8), (1,1,4),
              (2,1,2), (-2,2,1), (-1,2,2), (0,2,4), (1,2,2), (2,2,1)]
    for y in range(HEIGHT):
        print(f"\rProcessing: {y + 1}/{HEIGHT}", end="", flush=True)
        direction = 1 if y % 2 == 0 else -1
        xs = range(WIDTH) if direction == 1 else range(WIDTH - 1, -1, -1)
        for x in xs:
            rgb = tuple(clamp(v) for v in work[y][x])
            panel = nearest_color(rgb)
            codes[y][x] = panel["code"]
            strength = local_strength(edge[x, y])
            errors = [max(-MAX_ERROR, min(MAX_ERROR, (rgb[i] - panel["rgb"][i]) * strength)) for i in range(3)]
            for dx, dy, weight in kernel:
                add_error(work, x + dx * direction, y + dy, *errors, weight / 42)
    print()
    return codes


def create_preview(codes, path: Path):
    preview = Image.new("RGB", (WIDTH, HEIGHT))
    pixels = preview.load()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            pixels[x, y] = PALETTE_BY_CODE[codes[y][x]]["rgb"]
    preview.save(path)


def pack_image(codes) -> bytearray:
    return bytearray((codes[y][x] << 4) | codes[y][x + 1]
                     for y in range(HEIGHT) for x in range(0, WIDTH, 2))


def write_header(data: bytes, output: Path):
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <Arduino.h>\n\nconst uint8_t epaper_image[] PROGMEM = {\n")
        for i, value in enumerate(data):
            if i % 16 == 0:
                f.write("    ")
            f.write(f"0x{value:02X}")
            f.write(", " if i + 1 < len(data) else "")
            if i % 16 == 15:
                f.write("\n")
        f.write(f"\n}};\n\nconst uint32_t epaper_image_size = {len(data)};\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="input JPEG/PNG/etc.")
    parser.add_argument("output", type=Path, help="output image_data.h")
    parser.add_argument("--preview", type=Path, default=Path("epaper_preview.png"))
    parser.add_argument("--processed", type=Path, default=Path("epaper_source_processed.png"))
    parser.add_argument("--edges", type=Path, default=Path("epaper_edges.png"))
    args = parser.parse_args()

    img = preprocess(resize_and_crop(Image.open(args.input).convert("RGB")))
    img.save(args.processed)
    edge_map = create_edge_map(img)
    edge_map.save(args.edges)
    codes = stucki_dither(img, edge_map)
    create_preview(codes, args.preview)
    data = pack_image(codes)
    if len(data) != 192000:
        raise RuntimeError(f"invalid packed size: {len(data)}")
    write_header(data, args.output)
    counts = {c["code"]: 0 for c in PALETTE}
    for row in codes:
        for code in row:
            counts[code] += 1
    print("Color usage:")
    for color in PALETTE:
        print(f"  {color['name']:6s}: {counts[color['code']] * 100 / (WIDTH * HEIGHT):6.2f}%")
    print(f"Header: {args.output}\nPreview: {args.preview}\nData: {len(data)} bytes")


if __name__ == "__main__":
    main()
