#!/usr/bin/env python3
"""
Generate starter cold / temperate / hot / corrupted lookup textures for world_generator.py.

Each texture: X axis = height (left = flat, right = mountains),
              Y axis = wetness (top row = wettest).
Paint over them afterwards, or edit the rules below.

Usage:  python make_lookups.py --size 64 --out-dir .
"""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image

from biomes import BIOME_COLORS

SEA_LEVEL = 0.44


def cold_biome(height, wetness):
    if height < SEA_LEVEL:
        return "cold_ocean"
    if height > 0.55:
        return "snowy_mountains"
    if wetness > 0.55 and height > 0.49:
        return "glacier"
    if wetness > 0.50:
        return "taiga"
    return "tundra"


def temperate_biome(height, wetness):
    if height < SEA_LEVEL:
        return "ocean"
    if height > 0.65:
        return "snowy_mountains"
    if height > 0.60:
        return "mountains"
    if wetness > 0.55 and height < 0.47:
        return "swamp"
    if wetness > 0.53:
        return "forest"
    return "plains"


def hot_biome(height, wetness):
    if height < SEA_LEVEL:
        return "warm_ocean"
    if height > 0.68:
        return "mountains"
    if height > 0.60 and wetness < 0.45:
        return "mesa"
    if wetness > 0.45:
        return "savanna" if height < 0.535 else "jungle"
    return "desert"


def corrupted_biome(height, wetness):
    if height < SEA_LEVEL - 0.30:
        return "void"
    if height < SEA_LEVEL - 0.19:
        return "void_shore"
    if height > 0.65 or (wetness < 0.5 and wetness > 0.45):
        return "crags"
    if wetness > 0.57 and height < 0.47:
        return "acidic_marsh"
    if wetness < 0.45:
        return "wasteland"
    return "red_wastes"


def build_lookup(classify, size):
    rgb = np.zeros((size, size, 3), dtype=np.uint8)
    for row in range(size):
        wetness = 1.0 - row / (size - 1)
        for col in range(size):
            height = col / (size - 1)
            rgb[row, col] = BIOME_COLORS[classify(height, wetness)]
    return rgb


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--size", type=int, default=64, help="Texture width/height")
    parser.add_argument("--out-dir", default=".", help="Output directory")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    for name, fn in (("cold_biomes", cold_biome),
                     ("temperate_biomes", temperate_biome),
                     ("hot_biomes", hot_biome),
                     ("corrupted_biomes", corrupted_biome)):
        path = out_dir / f"{name}.png"
        Image.fromarray(build_lookup(fn, args.size), "RGB").save(path)
        print(f"Wrote {path}")


if __name__ == "__main__":
    main()