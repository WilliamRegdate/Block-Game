#!/usr/bin/env python3
"""
Generates 3 starter lookup textures (cold / temperate / hot) that
world_generator.py can consume right away. These are placeholders -
open them in an image editor and paint over them, or replace them
entirely once you know what you want each band to look like.

Each texture is small (default 64x64): X axis = height, Y axis = wetness,
same convention as world_generator.py's default (row 0 = wettest).

Usage:
    python generate_example_lookups.py --size 64 --out-dir .
"""

import argparse

import numpy as np
from PIL import Image

BIOME_COLORS = {
    "ocean":           (39,  85, 191),
    "cold_ocean":      (31,  31, 191),
    "warm_ocean":      (83,  160, 191),
    "snowy_mountains": (235, 235, 240),
    "mountains":       (130, 130, 140),
    "tundra":          (170, 190, 210),
    "taiga":           (60, 100,  70),
    "glacier":         (180, 250, 250),
    "desert":          (224, 218, 168),
    "savanna":         (100, 116,  30),
    "jungle":          (70, 150,  30),
    "plains":          (80, 185,  70),
    "forest":          (0, 100,  10),
    "swamp":           (110, 88,  70),
    "mesa":            (168, 96,   0),
    "cursed_forest":   (80,  20,  20), #evil
    "evil_tundra":     (80,  40,  80),
    "evil_hills":      (70,  50,  40),
    "evil_desert":     (110, 50,  20),
    "evil_plains":     (100, 30,  30),
    "magic_forest":    (40, 180, 120),  #good
    "sacred_tundra":   (220, 245, 255),
    "magical_hills":   (150, 100, 220),
    "holy_desert":     (245, 220, 100),
    "holy_plains":     (180, 240, 100)
}


def cold_biome(height, wetness):
    if height < 0.43:
        return "cold_ocean"
    if height > 0.55:
        return "snowy_mountains"
    if wetness > 0.55 and height > 0.49:
        return "glacier"
    if wetness > 0.50:
        return "taiga"
    
    return "tundra"


def temperate_biome(height, wetness):
    if height < 0.43:
        return "ocean"
    if height > 0.65:
        return "snowy_mountains"
    if height > 0.6:
        return "mountains"
    if wetness > 0.55 and height < 0.47:
        return "swamp"
    if wetness > 0.53:
        return "forest"
    
    return "plains"


def hot_biome(height, wetness):
    if height < 0.43:
        return "warm_ocean"
    if height > 0.68:
        return "mountains"
    if height > 0.6 and wetness < 0.45:
        return "mesa"
    if wetness > 0.45 and height < 0.535:
        return "savanna"
    if wetness > 0.45:
        return "jungle"
    return "desert"


def build_lookup(classify_fn, size):
    rgb = np.zeros((size, size, 3), dtype=np.uint8)

    for row in range(size):
        wetness = 1.0 - row / (size - 1)

        for col in range(size):
            height = col / (size - 1)
            rgb[row, col] = BIOME_COLORS[classify_fn(height, wetness)]

    return rgb


def main():
    parser = argparse.ArgumentParser(
        description="Generate starter cold/temperate/hot lookup textures."
    )

    parser.add_argument(
        "--size",
        type=int,
        default=64,
        help="Width/height of each lookup texture"
    )

    parser.add_argument(
        "--out-dir",
        default=".",
        help="Directory to write the 3 PNGs into"
    )

    args = parser.parse_args()

    for name, fn in (
        ("cold_biomes", cold_biome),
        ("temperate_biomes", temperate_biome),
        ("hot_biomes", hot_biome)
    ):
        rgb = build_lookup(fn, args.size)
        path = f"{args.out_dir}/{name}.png"
        Image.fromarray(rgb, "RGB").save(path)
        print(f"Wrote {path}")


if __name__ == "__main__":
    main()