#include "WorldGenerator.h"
#include <algorithm>
namespace
{
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
        { -0.85f, -35.0f },  // deep ocean
        { -0.7f, 15.0f },  // ocean shelf 
        { -0.32f, 38.0f },  // ocean floor 
        { -0.026f, 58.0f }, // steep rise at the coast
        {  0.0f, 64.0f },  // beach (sea level ~62)
        {  0.01f, 70.0f },  // beach slope in (sea level ~62)
        {  0.3f, 70.0f },  // flat inland plains
        {  0.5f, 71.0f },  // highlands start
        {  0.6f, 100.0f },  // highlands shelf
        {  0.61f, 105.0f },  // highlands
        {  0.8f, 108.0f },  // mountains start
        {  0.85f, 210.0f }  // mountains
    };
    static const std::vector<SplinePoint> kErosion = {
        { -0.85f, 0.03 }, //flats
        { -0.2f, 0.09 }, //flats end
        { 0.0f, 0.1 }, //medium
        { 0.4f, 0.5 }, //medium end
        { 0.65f, 1.5 }, //high jaggedy

    };
}


WorldGenerator::WorldGenerator(int64_t seed)
    : seed(seed)
{
    continentalness.SetSeed(seed);
    wetness.SetSeed(seed+1);
    temperature.SetSeed(seed+2);
    height.SetSeed(seed+3);
    height2.SetSeed(seed+4);
    erosion.SetSeed(seed+5);


    height.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    height.SetFractalType(FastNoiseLite::FractalType_FBm);
    height.SetFractalOctaves(3);
    height.SetFractalGain(0.65f);
    height.SetFrequency(0.01f);
    
    height2.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    height2.SetFractalType(FastNoiseLite::FractalType_FBm);
    height2.SetFractalOctaves(3);
    height2.SetFractalGain(0.65f);
    height2.SetFrequency(0.01f);


    erosion.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    erosion.SetFractalType(FastNoiseLite::FractalType_None);
    erosion.SetFrequency(0.01f);

    continentalness.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    continentalness.SetFractalType(FastNoiseLite::FractalType_None);
    continentalness.SetFrequency(0.0009f);

    wetness.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    wetness.SetFractalType(FastNoiseLite::FractalType_None);
    wetness.SetFrequency(0.003f);

    temperature.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    temperature.SetFractalType(FastNoiseLite::FractalType_None);
    temperature.SetFrequency(0.002f);

}

Voxel WorldGenerator::GetBlockAt(int x, int y, int z)
{
    float t = std::clamp(temperature.GetNoise(float(x), float(z)) * 0.5f + float(z * 0.0009f), -1.0f, 1.0f);

    float w = wetness.GetNoise(float(x),float(z));
    float c = continentalness.GetNoise((float)x, (float)z);


    float e = erosion.GetNoise(float(x), float(z));

    
    float baseHeight = c;
    baseHeight += height.GetNoise(float(x), float(z)) * (SampleSpline(kErosion, e)) * 50 + DuneHeight(x*0.2, z*0.2) * 30;
    
    Voxel base = stone;
    switch (GetBiome(x, y, z, c, w, t))
    {
        case Biome::Swamp:
            base = mud;
            break;
        case Biome::Ocean:
            base = water;
            break;
        case Biome::ColdOcean:
            base = water;
            break;
        case Biome::WarmOcean:
            base = water;
            break;
        case Biome::Desert:
            base = sand;
            break;
        case Biome::Forest:
            base = log;
            break;
        case Biome::Jungle:
            base = log;
            break;
        case Biome::Mountains:
            base = stone;
            break;
        case Biome::SnowyMountains:
            base = snow;
            break;
        case Biome::Tundra:
            base = snow;
            break;
        case Biome::Taiga:
            base = log;
            break;
        case Biome::Plains:
            base = grass;
            break;
        case Biome::Savanna:
            base = grass;
            break;
        case Biome::Mesa:
            base = redSand;
            break;
    }
    
    return y > baseHeight ? air : base;

}
Voxel WorldGenerator::GetBlockAt(glm::vec3 position)
{
    return GetBlockAt(position.x, position.y, position.z);
}

float WorldGenerator::DuneHeight(float x, float z)
{
    return std::min(height2.GetNoise(x, z), height.GetNoise(x, z));
}

WorldGenerator::Biome WorldGenerator::GetBiome(int x, int y, int z, float height, float wetness, float temperature)
{
    if (x * x + z * z > 2500000)
        return CorruptedBiome(height, wetness);
    else if ( temperature < COLD_START)
        return ColdBiome(height, wetness);
    else if ( temperature > HOT_START)
        return HotBiome(height, wetness);    
    else
        return TemperateBiome(height, wetness);    

}

WorldGenerator::Biome WorldGenerator::ColdBiome(float height, float wetness)
{
    if (height < SEA_LEVEL)                    return Biome::ColdOcean;
    if (height > 0.60f)                        return Biome::SnowyMountains;
    if (wetness > 0.30f && height > 0.35f)     return Biome::Glacier;
    if (wetness > 0.15f)                       return Biome::Taiga;
    return Biome::Tundra;
}

WorldGenerator::Biome WorldGenerator::TemperateBiome(float height, float wetness)
{
    if (height < SEA_LEVEL)                    return Biome::Ocean;
    if (height > 0.69f)                        return Biome::SnowyMountains;
    if (height > 0.45f)                        return Biome::Mountains;
    if (wetness > 0.25f && height < -0.15f)    return Biome::Swamp;
    if (wetness > 0.10f)                       return Biome::Forest;
    return Biome::Plains;
}

WorldGenerator::Biome WorldGenerator::HotBiome(float height, float wetness)
{
    if (height < SEA_LEVEL)                    return Biome::WarmOcean;
    if (height > 0.65f)                        return Biome::Mountains;
    if (height > 0.40f && wetness < -0.20f)     return Biome::Mesa;
    if (wetness > -0.10f)
        return height < 0.10f ? Biome::Savanna : Biome::Jungle;
    return Biome::Desert;
}

WorldGenerator::Biome WorldGenerator::CorruptedBiome(float height, float wetness)
{
    if (height < SEA_LEVEL - 0.45f)            return Biome::Void;
    if (height < SEA_LEVEL - 0.25f)            return Biome::VoidShore;
    if (height > 0.60f || (wetness < 0.0f && wetness > -0.20f))
                                               return Biome::Crags;
    if (wetness > 0.30f && height < -0.15f)    return Biome::AcidicMarsh;
    if (wetness < -0.20f)                      return Biome::Wasteland;
    return Biome::RedWastes;
}
