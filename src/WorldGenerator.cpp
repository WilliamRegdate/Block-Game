#include "WorldGenerator.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    inline float Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    struct SplinePoint { float x; float y; };

    float SampleSpline(const std::vector<SplinePoint>& pts, float x)
    {
        if (x <= pts.front().x) return pts.front().y;
        if (x >= pts.back().x)  return pts.back().y;

        for (size_t i = 1; i < pts.size(); ++i)
        {
            if (x <= pts[i].x)
            {
                const SplinePoint& a = pts[i - 1];
                const SplinePoint& b = pts[i];
                float t = (x - a.x) / (b.x - a.x);
                return a.y + (b.y - a.y) * t;
            }
        }
        return pts.back().y;
    }

    static const std::vector<SplinePoint> kContinentalness = {
        { -0.85f,  15.0f },   // deep ocean
        { -0.75f,  30.0f },   // ocean floor
        { -0.60f,  48.0f },   // ocean slope
        { -0.45f,  62.0f },   // shoreline (just under water)
        { -0.30f,  65.0f },   // beach
        { -0.20f,  66.0f },   // flat shelf starts
        {  0.20f,  66.0f },   // flat shelf ends
        {  0.67f,  90.0f },   // foothills
        {  0.75f,  92.0f },   // foothills shelf
        {  0.82f, 120.0f },   // hills
        {  0.93f, 210.0f },   // mountains
        {  1.00f, 350.0f }    // peaks
    };

    // Divides the vertical bias: small = flat terrain, large = noisy / jagged terrain
    static const std::vector<SplinePoint> kErosion = {
        { -1.0f,  1.0f },   // flats
        { -0.2f,  2.0f },   // flats end
        {  0.0f,  6.0f },
        {  0.4f, 20.0f },   // medium
        {  1.0f, 60.0f },   // high jaggedy
    };

    // The fractal noise stays within about +-1, so once |bias| passes this the
    // noise can no longer change solid/air and we can skip sampling it.
    constexpr float kNoiseReach = 1.0f;

    // Mountain faces
    constexpr float kSteepSlope = 1.0f;   // rise per block of run: steeper than this is stone
    constexpr float kCliffSlope = 1.6f;   // steeper than this is cobblestone
    constexpr float kSnowLine   = 150.0f;
}

WorldGenerator::WorldGenerator(int64_t worldSeed)
    : seed(static_cast<int32_t>(worldSeed))
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

Voxel WorldGenerator::GetBlockAt(glm::vec3 position)
{
    return GetBlockAt(int(std::floor(position.x)),
                      int(std::floor(position.y)),
                      int(std::floor(position.z)));
}

// Continentalness + erosion -> base height for one column (including terracing).
float WorldGenerator::ColumnShape(int x, int z, float& baseHeight, float& erosionValue) const
{
    const float fx = float(x), fz = float(z);

    const float c = continentalness.GetNoise(fx, fz);
    erosionValue = SampleSpline(kErosion, erosion.GetNoise(fx, fz));
    baseHeight = SampleSpline(kContinentalness, c) + height.GetNoise(fx, fz) * 5.0f;

    // Terracing: below this erosion value the land rises in steps.
    // NOTE: kErosion now bottoms out at 1.0, so with a threshold of 0.1 this never
    // triggers. Raise kStepThreshold (e.g. to 2.0) and kStepBlend (e.g. 0.5) to use it.
    constexpr float kStepThreshold = 0.1f;
    constexpr float kStepBlend     = 0.03f;
    constexpr float kStepHeight    = 6.0f;

    const float stepAmount = std::clamp((kStepThreshold - erosionValue) / kStepBlend, 0.0f, 1.0f);
    if (stepAmount > 0.0f)
    {
        const float stepped = std::floor(baseHeight / kStepHeight) * kStepHeight;
        baseHeight = Lerp(baseHeight, stepped, stepAmount);
    }
    return c;
}

float WorldGenerator::Density(int x, int y, int z) const
{
    float baseHeight, erosionValue;
    ColumnShape(x, z, baseHeight, erosionValue);

    const float bias = (float(y) - baseHeight) / erosionValue;
    return highNoise.GetNoise(float(x), float(y) * 0.8f, float(z)) - bias;
}

// Density falls by about 1/erosion per block going up, so
//   slope = |horizontal gradient of density| * erosion
float WorldGenerator::Slope(int x, int y, int z, float erosionValue) const
{
    const float dx = Density(x + 1, y, z) - Density(x - 1, y, z);
    const float dz = Density(x, y, z + 1) - Density(x, y, z - 1);
    return std::sqrt(dx * dx + dz * dz) * 0.5f * erosionValue;
}

Voxel WorldGenerator::GetBlockAt(int x, int y, int z)
{
    if (y < -1000) return air;

    const float fx = float(x), fz = float(z);

    const float t = std::clamp(temperature.GetNoise(fx, fz) * 0.0035f + fz * 0.0003f, -1.0f, 1.0f);
    const float w = wetness.GetNoise(fx, fz);

    // ---------------------------------------------------------------
    // 1. Terrain shape (the same for every biome)
    // ---------------------------------------------------------------
    float baseHeight, erosionValue;
    const float c = ColumnShape(x, z, baseHeight, erosionValue);

    // Is the block at height yy solid? Skips the noise when it can't matter.
    auto solidAt = [&](int yy)
    {
        const float bias = (float(yy) - baseHeight) / erosionValue;
        if (bias >  kNoiseReach) return false;
        if (bias < -kNoiseReach) return true;
        return highNoise.GetNoise(fx, float(yy) * 0.8f, fz) - bias > 0.0f;
    };

    // ---------------------------------------------------------------
    // 2. Biome decides which blocks fill that shape
    // ---------------------------------------------------------------
    const Biome biome = GetBiome(x, z, c, w, t);
    if (biome == Biome::Void) return air;

    if (!solidAt(y))
        return y <= WATER_LEVEL ? water : air;

    Voxel top = grass;     // block with air/water above it
    Voxel filler = mud;    // the few blocks under the top

    switch (biome)
    {
        case Biome::Swamp:
        case Biome::AcidicMarsh:
        case Biome::Ocean:
        case Biome::ColdOcean:
            top = mud;   filler = mud;   break;

        case Biome::Desert:
        case Biome::WarmOcean:
        case Biome::VoidShore:
            top = sand;  filler = sand;  break;

        case Biome::Mesa:
        case Biome::RedWastes:
            top = redSand; filler = redSand; break;

        case Biome::Glacier:
            top = snow;  filler = snow;  break;

        case Biome::Tundra:
            top = snow;  filler = mud;   break;

        case Biome::Jungle:
        case Biome::Taiga:
            top = log;   filler = mud;   break;

        case Biome::Crags:
            top = stone; filler = stone; break;

        case Biome::Wasteland:
            top = mud;   filler = stone; break;

        default:   // Forest, Plains, Savanna, Mountains and SnowyMountains (handled below)
            break;
    }

    const bool isTop = !solidAt(y + 1);
    if (!isTop && solidAt(y + 4)) return stone;   // deep underground

    // Mountains: steep faces are cobblestone; shallow faces are snow above the
    // snow line and grass below it. (SnowyMountains is snow on every shallow face.)
    if (biome == Biome::Mountains || biome == Biome::SnowyMountains)
    {
        const float slope = Slope(x, y, z, erosionValue);

        if (slope > kCliffSlope) return cobblestone;
        if (slope > kSteepSlope) return stone;

        if (biome == Biome::SnowyMountains || float(y) >= kSnowLine)
            return isTop ? snow : stone;

        return isTop ? grass : mud;
    }

    return isTop ? top : filler;
}

// ---------------------------------------------------------------------------
// Biomes
// ---------------------------------------------------------------------------
WorldGenerator::Biome WorldGenerator::GetBiome(int x, int z, float c, float w, float t)
{
    if (int64_t(x) * x + int64_t(z) * z > 100000000)
        return CorruptedBiome(c, w);
    if (t < COLD_START) return ColdBiome(c, w);
    if (t > HOT_START)  return HotBiome(c, w);
    return TemperateBiome(c, w);
}

WorldGenerator::Biome WorldGenerator::ColdBiome(float c, float w)
{
    if (c < SEA_LEVEL)                  return Biome::ColdOcean;
    if (c > 0.60f)                      return Biome::SnowyMountains;
    if (w > 0.30f && c > 0.35f)         return Biome::Glacier;
    if (w > 0.15f)                      return Biome::Taiga;
    return Biome::Tundra;
}

WorldGenerator::Biome WorldGenerator::TemperateBiome(float c, float w)
{
    if (c < SEA_LEVEL)                  return Biome::Ocean;
    if (c > 0.89f)                      return Biome::SnowyMountains;
    if (c > 0.75f)                      return Biome::Mountains;
    if (w > 0.25f && c < -0.15f)        return Biome::Swamp;
    if (w > 0.10f)                      return Biome::Forest;
    return Biome::Plains;
}

WorldGenerator::Biome WorldGenerator::HotBiome(float c, float w)
{
    if (c < SEA_LEVEL)                  return Biome::WarmOcean;
    if (c > 0.65f)                      return Biome::Mountains;
    if (c > 0.40f && w < -0.20f)        return Biome::Mesa;
    if (w > -0.10f)
        return c < 0.10f ? Biome::Savanna : Biome::Jungle;
    return Biome::Desert;
}

WorldGenerator::Biome WorldGenerator::CorruptedBiome(float c, float w)
{
    if (c < SEA_LEVEL - 0.45f)          return Biome::Void;
    if (c < SEA_LEVEL - 0.25f)          return Biome::VoidShore;
    if (c > 0.60f || (w < 0.0f && w > -0.20f))
        return Biome::Crags;
    if (w > 0.30f && c < -0.15f)        return Biome::AcidicMarsh;
    if (w < -0.20f)                     return Biome::Wasteland;
    return Biome::RedWastes;
}