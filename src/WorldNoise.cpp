#include "Generation/WorldNoise.h"
#include <algorithm>

WorldNoise::WorldNoise(int32_t seed)
{
    continentalness.SetSeed(seed);
    wetness.SetSeed(seed + 1);
    temperature.SetSeed(seed + 2);
    erosion.SetSeed(seed + 3);
    highNoise.SetSeed(seed + 4);
    height.SetSeed(seed + 5);

    highNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    highNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    highNoise.SetFrequency(0.01f);
    highNoise.SetFractalOctaves(4);
    highNoise.SetFractalLacunarity(2.0f);
    highNoise.SetFractalGain(0.62f);

    height.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    height.SetFractalType(FastNoiseLite::FractalType_FBm);
    height.SetFrequency(0.01f);
    height.SetFractalOctaves(4);
    height.SetFractalLacunarity(2.0f);
    height.SetFractalGain(0.62f);

    erosion.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    erosion.SetFractalType(FastNoiseLite::FractalType_None);
    erosion.SetFrequency(0.0035f);

    continentalness.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    continentalness.SetFractalType(FastNoiseLite::FractalType_FBm);
    continentalness.SetFractalOctaves(3);
    continentalness.SetFrequency(0.0009f);
    continentalness.SetFractalLacunarity(5.0f);
    continentalness.SetFractalGain(0.1f);

    wetness.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    wetness.SetFractalType(FastNoiseLite::FractalType_None);
    wetness.SetFrequency(0.003f);

    temperature.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    temperature.SetFractalType(FastNoiseLite::FractalType_None);
    temperature.SetFrequency(0.002f);
}

Climate WorldNoise::Sample(int x, int z) const
{
    const float fx = float(x), fz = float(z);
    Climate c;
    c.continentalness = continentalness.GetNoise(fx, fz);
    c.wetness         = wetness.GetNoise(fx, fz);
    c.temperature     = std::clamp(temperature.GetNoise(fx, fz) * 0.0035f + fz * 0.0003f, -1.0f, 1.0f);
    return c;
}