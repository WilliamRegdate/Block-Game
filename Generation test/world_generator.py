#!/usr/bin/env python3
"""
World biome generator.

1. Classify each pixel as cold / temperate / hot from the temperature map.
2. Look up its biome colour in that band's texture
   (X = height, Y = wetness; top row = wettest unless --wet-at-bottom).
3. Swap land biomes for good / evil variants based on the evil map.
4. Distance rings, measured from the image centre as 0..1 where 1.0 is the
   distance to the nearest edge (corners are > 1):
       distance <  far_lands_start      normal world
       far_lands_start <= d < corrupted_start   solid "far_lands" biome
       distance >= corrupted_start      corrupted_biomes.png LUT instead
                                        of the temperature LUTs

Inputs are grayscale PNGs (0 = black, 1 = white), resized to the
temperature map's size:
    height, wetness, temperature, evil

Usage:  python world_generator.py                (uses the defaults below)
        python world_generator.py --output my_world.png --wet-at-bottom
"""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image

from biomes import ALIGNMENT_VARIANTS, BIOME_COLORS

BAND_NAMES = ("cold", "temperate", "hot")


def load_gray01(path, size):
    """Load as grayscale in 0..1, resized to `size` (w, h) if needed."""
    img = Image.open(path).convert("L")
    if img.size != size:
        img = img.resize(size, Image.BILINEAR)
    return np.asarray(img, dtype=np.float32) / 255.0


def load_rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)


def classify_temperature(temperature, cold_max, hot_min):
    """0 = cold, 1 = temperate, 2 = hot."""
    band = np.ones(temperature.shape, dtype=np.uint8)
    band[temperature <= cold_max] = 0
    band[temperature >= hot_min] = 2
    return band


def sample_lookup(lookup, height, wetness, wet_at_bottom=False):
    """Sample an (H, W, 3) lookup texture at 1D arrays of height/wetness."""
    lu_h, lu_w = lookup.shape[:2]
    x = np.clip(np.round(height * (lu_w - 1)), 0, lu_w - 1).astype(int)
    y_frac = wetness if wet_at_bottom else 1.0 - wetness
    y = np.clip(np.round(y_frac * (lu_h - 1)), 0, lu_h - 1).astype(int)
    return lookup[y, x]


def distance_from_centre(shape):
    """
    Normalised distance of every pixel from the image centre.
    0.0 = centre, 1.0 = the nearest edge (corners come out above 1.0).
    """
    h, w = shape
    ys, xs = np.indices((h, w), dtype=np.float32)
    dist = np.hypot(xs - (w - 1) / 2, ys - (h - 1) / 2)
    return dist / (min(h, w) / 2)


def edge_taper(dist, taper_start, taper_end):
    """
    Height multiplier: 1.0 up to taper_start, easing smoothly down to 0.0
    at taper_end (smoothstep), and 0.0 beyond.
    """
    t = np.clip((dist - taper_start) / (taper_end - taper_start), 0.0, 1.0)
    return 1.0 - t * t * (3.0 - 2.0 * t)


def apply_alignment(world, evil, good_max, evil_min):
    """Replace base biome colours with good/evil variants (vectorised)."""
    original = world.copy()
    is_good = evil < good_max
    is_evil = evil > evil_min

    for base, (good, bad) in ALIGNMENT_VARIANTS.items():
        is_base = np.all(original == BIOME_COLORS[base], axis=-1)
        world[is_base & is_good] = BIOME_COLORS[good]
        world[is_base & is_evil] = BIOME_COLORS[bad]
    return world


def generate_world(paths, output_path, cold_max=1 / 3, hot_min=2 / 3,
                   good_max=0.34, evil_min=0.64, wet_at_bottom=False,
                   far_lands_start=0.7, corrupted_start=0.72,
                   taper_start=0.9, taper_end=1.0):
    temperature_img = Image.open(paths["temperature"]).convert("L")
    size = temperature_img.size
    temperature = np.asarray(temperature_img, dtype=np.float32) / 255.0

    height = load_gray01(paths["height"], size)
    wetness = load_gray01(paths["wetness"], size)
    evil = load_gray01(paths["evil"], size)

    dist = distance_from_centre(temperature.shape)
    height = height * edge_taper(dist, taper_start, taper_end)

    band = classify_temperature(temperature, cold_max, hot_min)
    lookups = [load_rgb(paths[name]) for name in BAND_NAMES]

    world = np.zeros((*temperature.shape, 3), dtype=np.uint8)
    for band_id, lookup in enumerate(lookups):
        mask = band == band_id
        if mask.any():
            world[mask] = sample_lookup(lookup, height[mask], wetness[mask],
                                        wet_at_bottom)

    # Good/evil only applies to the normal world.
    world = apply_alignment(world, evil, good_max, evil_min)

    far_mask = (dist >= far_lands_start) & (dist < corrupted_start)
    world[far_mask] = BIOME_COLORS["far_lands"]

    corrupted_mask = dist >= corrupted_start
    if corrupted_mask.any():
        world[corrupted_mask] = sample_lookup(
            load_rgb(paths["corrupted"]),
            height[corrupted_mask], wetness[corrupted_mask], wet_at_bottom)

    Image.fromarray(world, "RGB").save(output_path)

    print(f"Saved {output_path} ({size[0]}x{size[1]})")
    for band_id, name in enumerate(BAND_NAMES):
        print(f"  {name:<10} {np.mean(band == band_id) * 100:5.1f}%")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--dir", default=".", help="Folder containing the input PNGs")
    parser.add_argument("--output", default="world.png")
    parser.add_argument("--wet-at-bottom", action="store_true")
    parser.add_argument("--cold-max", type=float, default=1 / 3)
    parser.add_argument("--hot-min", type=float, default=2 / 3)
    parser.add_argument("--good-max", type=float, default=0.34)
    parser.add_argument("--evil-min", type=float, default=0.64)
    parser.add_argument("--far-lands-start", type=float, default=0.67,
                        help="Distance from centre (0-1, 1 = nearest edge) where the far lands begin")
    parser.add_argument("--corrupted-start", type=float, default=0.72,
                        help="Distance from centre (0-1, 1 = nearest edge) where corrupted lands begin")
    parser.add_argument("--taper-start", type=float, default=0.9,
                        help="Distance (0-1) where height starts tapering down")
    parser.add_argument("--taper-end", type=float, default=1.0,
                        help="Distance (0-1) where height reaches 0")
    args = parser.parse_args()

    d = Path(args.dir)
    paths = {
        "height": d / "height.png",
        "wetness": d / "wetness.png",
        "temperature": d / "temperature.png",
        "evil": d / "evil.png",
        "cold": d / "cold_biomes.png",
        "temperate": d / "temperate_biomes.png",
        "hot": d / "hot_biomes.png",
        "corrupted": d / "corrupted_biomes.png",
    }

    generate_world(paths, args.output, args.cold_max, args.hot_min,
                   args.good_max, args.evil_min, args.wet_at_bottom,
                   args.far_lands_start, args.corrupted_start,
                   args.taper_start, args.taper_end)


if __name__ == "__main__":
    main()