// WorldNoise.h
#pragma once
#include <cstdint>
#include "FastNoiseLite.h"

struct Climate { float continentalness; float wetness; float temperature; };

// Read-only after construction, so workers can share it.
struct WorldNoise
{
    explicit WorldNoise(int32_t seed);
    Climate Sample(int x, int z) const;

    FastNoiseLite continentalness, wetness, temperature, erosion;
    FastNoiseLite highNoise, height, cragsCanyon, cragsTop, cracks;
};