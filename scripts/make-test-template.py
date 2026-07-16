#!/usr/bin/env python3
"""Generate a conspicuous native-size Condor notebook test template."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


WIDTH = 1404
HEIGHT = 1872


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    return parser.parse_args()


def centered_text(draw: ImageDraw.ImageDraw, y: int, text: str, size: int) -> None:
    font = ImageFont.load_default(size=size)
    box = draw.textbbox((0, 0), text, font=font, stroke_width=2)
    width = box[2] - box[0]
    draw.text(
        ((WIDTH - width) // 2, y),
        text,
        font=font,
        fill=(0, 230),
        stroke_width=2,
        stroke_fill=(0, 230),
    )


def main() -> int:
    args = parse_args()
    image = Image.new("LA", (WIDTH, HEIGHT), (0, 0))
    draw = ImageDraw.Draw(image)

    draw.rectangle((24, 24, WIDTH - 25, HEIGHT - 25), outline=(0, 220), width=8)
    for start in range(-HEIGHT, WIDTH, 180):
        draw.line((start, 0, start + HEIGHT, HEIGHT), fill=(0, 105), width=5)
    for y in range(160, HEIGHT, 240):
        draw.line((40, y, WIDTH - 41, y), fill=(0, 55), width=3)

    centered_text(draw, 690, "TEST TEMPLATE", 104)
    centered_text(draw, 850, "ISOMETRIC GRID SLOT", 58)
    centered_text(draw, 980, "FIRMWARE 4.38.23697", 48)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output, format="PNG", optimize=True, compress_level=9)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
