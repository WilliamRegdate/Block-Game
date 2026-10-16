"""Shared biome definitions used by both the lookup and world generators."""

BIOME_COLORS = {
    # oceans
    "ocean":           (39,  85, 191),
    "cold_ocean":      (31,  31, 191),
    "warm_ocean":      (83, 160, 191),
    # normal land
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
    # evil
    "cursed_forest":   (80,  20,  20),
    "evil_tundra":     (80,  40,  80),
    "evil_hills":      (70,  50,  40),
    "evil_desert":     (110, 50,  20),
    "evil_plains":     (100, 30,  30),
    # good
    "magic_forest":    (40, 180, 120),
    "sacred_tundra":   (220, 122, 255),
    "magical_hills":   (150, 100, 220),
    "holy_desert":     (245, 220, 100),
    "holy_plains":     (86, 229, 250),

    # far lands ring
    "far_lands":         (211, 204, 169),
    # corrupted lands (beyond the far lands)
    "red_wastes": (255, 96, 96),
    "crags":             (175, 62, 175),
    "acidic_marsh":      (76, 255, 0),
    "wasteland":         (128, 100, 100),
    "void_shore":        (62, 38, 38),
    "void":              (0, 0, 0),
}

# base biome -> (good variant, evil variant)
ALIGNMENT_VARIANTS = {
    "taiga":           ("magic_forest",  "cursed_forest"),
    "forest":          ("magic_forest",  "cursed_forest"),
    "jungle":          ("magic_forest",  "cursed_forest"),
    "tundra":          ("sacred_tundra", "evil_tundra"),
    "mountains":       ("magical_hills", "evil_hills"),
    "snowy_mountains": ("magical_hills", "evil_hills"),
    "desert":          ("holy_desert",   "evil_desert"),
    "plains":          ("holy_plains",   "evil_plains"),
    "savanna":         ("holy_plains",   "evil_plains"),
}