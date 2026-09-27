#!/usr/bin/env python3
"""
World Biome Generator - temperature-gated lookup tables
=========================================================

Step 1: classify every pixel of the world into a temperature band
        (cold / temperate / hot) from a grayscale temperature map.
Step 2: for each pixel, look up its biome color in that band's lookup
        texture, using height as the X axis and wetness as the Y axis.

World input images (grayscale, all describing the same world - sizes may
differ, they'll be resized to match the temperature map):
    height.png        black (0.0) = flat            -> white (1.0) = mountains
    wetness.png       black (0.0) = dry              -> white (1.0) = wettest
    temperature.png   black (0.0) = cold             -> white (1.0) = hot

Lookup textures (one per temperature band - can be any size, e.g. 64x64.
X axis = height, Y axis = wetness):
    cold_biomes.png / temperate_biomes.png / hot_biomes.png
    Column 0 (left)  = height 0.0 (flat)      Column -1 (right) = height 1.0 (mountains)
    Row 0 (top)      = wetness 1.0 (wettest)  Row -1 (bottom)   = wetness 0.0 (driest)
    (pass --wet-at-bottom if you'd rather paint your textures the other way up)

Usage:
    python world_generator.py \
        --height height.png --wetness wetness.png --temperature temperature.png \
        --cold cold_biomes.png --temperate temperate_biomes.png --hot hot_biomes.png \
        --output world.png
"""

import numpy as np
from PIL import Image


def load_gray01(path, size=None):
    """Load an image as grayscale, normalized to 0.0-1.0, optionally resized."""
    img = Image.open(path).convert("L")
    if size is not None and img.size != size:
        img = img.resize(size, Image.BILINEAR)
    return np.asarray(img, dtype=np.float32) / 255.0


def load_rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)


def classify_temperature(temperature, cold_max, hot_min):
    """Return an array of 0=cold, 1=temperate, 2=hot for each pixel."""
    band = np.ones(temperature.shape, dtype=np.uint8)
    band[temperature <= cold_max] = 0
    band[temperature >= hot_min] = 2
    return band


def sample_lookup(lookup_rgb, height, wetness, wet_at_bottom=False):
    """
    Sample a lookup texture for a batch of (height, wetness) values.
    height -> X axis (0=left, 1=right), wetness -> Y axis.
    height/wetness are 1D arrays of the same length here (already masked
    to one temperature band); returns an (N, 3) array of colors.
    """
    lu_h, lu_w = lookup_rgb.shape[:2]

    x = np.clip((height * (lu_w - 1)).round().astype(int), 0, lu_w - 1)

    if wet_at_bottom:
        y = np.clip((wetness * (lu_h - 1)).round().astype(int), 0, lu_h - 1)
    else:
        y = np.clip(((1.0 - wetness) * (lu_h - 1)).round().astype(int), 0, lu_h - 1)

    return lookup_rgb[y, x]


BIOME_COLORS = {
    "ocean":           (39,  85, 191),
    "cold_ocean":      (31,  31, 191),
    "warm_ocean":      (83, 160, 191),
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
    "cursed_forest":     (80,  20,  20),
    "evil_tundra":     (80,  40,  80),
    "evil_hills":      (70,  50,  40),
    "evil_desert":     (110, 50,  20),
    "evil_plains":     (100, 30,  30),
    "magic_forest":    (40, 180, 120),
    "sacred_tundra":   (220, 122, 255),
    "magical_hills":   (150, 100, 220),
    "holy_desert":     (245, 220, 100),
    "holy_plains":     (86, 229, 250)
}

COLOR_TO_BIOME = {
    color: biome
    for biome, color in BIOME_COLORS.items()
}


def apply_evil(biome, evil, good_max, bad_min):
    good_biomes = {
        "taiga": "magic_forest",
        "forest": "magic_forest",
        "jungle": "magic_forest",

        "tundra": "sacred_tundra",

        "mountains": "magical_hills",
        "snowy_mountains": "magical_hills",

        "desert": "holy_desert",

        "plains": "holy_plains",
        "savanna": "holy_plains",
    }

    evil_biomes = {
        "taiga": "cursed_forest",
        "forest": "cursed_forest",
        "jungle": "cursed_forest",

        "tundra": "evil_tundra",

        "mountains": "evil_hills",
        "snowy_mountains": "evil_hills",

        "desert": "evil_desert",

        "plains": "evil_plains",
        "savanna": "evil_plains",
    }

    if evil < good_max:
        return good_biomes.get(biome, biome)

    if evil > bad_min:
        return evil_biomes.get(biome, biome)

    return biome


def generate_world(height_path, wetness_path, temperature_path, evil_path,
                    cold_path, temperate_path, hot_path,
                    output_path, cold_max=1 / 3, hot_min=2 / 3,
                    evil_good_max=0.3, evil_bad_min=0.6,
                    wet_at_bottom=False):

    # Temperature map defines the reference world size; height/wetness are
    # resized to match if they don't already.
    temperature_img = Image.open(temperature_path).convert("L")
    size = temperature_img.size

    temperature = np.asarray(temperature_img, dtype=np.float32) / 255.0
    height = load_gray01(height_path, size)
    wetness = load_gray01(wetness_path, size)
    evil = load_gray01(evil_path, size)

    band = classify_temperature(temperature, cold_max, hot_min)

    lookups = {
        0: load_rgb(cold_path),
        1: load_rgb(temperate_path),
        2: load_rgb(hot_path),
    }

    h, w = temperature.shape
    world_rgb = np.zeros((h, w, 3), dtype=np.uint8)

    counts = {}
    for band_id, name in ((0, "cold"), (1, "temperate"), (2, "hot")):
        mask = band == band_id
        if not mask.any():
            continue

        colors = sample_lookup(
            lookups[band_id],
            height[mask],
            wetness[mask],
            wet_at_bottom
        )

        evil_values = evil[mask]

        for i, color in enumerate(colors):
            biome = COLOR_TO_BIOME.get(tuple(color))

            if biome is None:
                continue

            biome = apply_evil(
                biome,
                evil_values[i],
                evil_good_max,
                evil_bad_min
            )

            colors[i] = BIOME_COLORS[biome]

        world_rgb[mask] = colors
        counts[name] = int(mask.sum())

    Image.fromarray(world_rgb, "RGB").save(output_path)

    total = h * w
    print(f"Saved world map to {output_path}  ({w}x{h})")
    print("Temperature band distribution:")
    for name, n in counts.items():
        print(f"  {name:<10} {n / total * 100:5.1f}%")


def main():
    # --- Edit these paths / settings directly instead of passing CLI args ---
    height_path = "height.png"
    wetness_path = "wetness.png"
    temperature_path = "temperature.png"

    cold_path = "cold_biomes.png"
    temperate_path = "temperate_biomes.png"
    hot_path = "hot_biomes.png"

    evil_path = "evil.png"

    output_path = "world.png"

    cold_max = 1 / 3
    hot_min = 2 / 3
    evil_good_max = 0.34
    evil_bad_min = 0.64

    wet_at_bottom = False
    # --------------------------------------------------------------------

    generate_world(
        height_path,
        wetness_path,
        temperature_path,
        evil_path,
        cold_path,
        temperate_path,
        hot_path,
        output_path,
        cold_max,
        hot_min,
        evil_good_max,
        evil_bad_min,
        wet_at_bottom
    )


if __name__ == "__main__":
    main()