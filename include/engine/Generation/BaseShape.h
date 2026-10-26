#pragma once
#include "Generation/TerrainShape.h"

struct RollingParams
{
    const std::vector<SplinePoint>* heightSpline;
    const std::vector<SplinePoint>* erosionSpline;
    float heightNoiseAmp = 5.0f;
    float stepThreshold  = 0.1f;   // terracing; see the note in the old ColumnShape
    float stepBlend      = 0.03f;
    float stepHeight     = 6.0f;
};

class RollingShape : public TerrainShape
{
public:
    RollingShape(const WorldNoise& n, RollingParams p) : TerrainShape(n), params(p) {}
    ColumnInfo Column(int x, int z, const Climate& climate) const override;
    float Density(const ColumnInfo& col, int x, int y, int z) const override;
    bool  Solid(const ColumnInfo& col, int x, int y, int z) const override;
private:
    RollingParams params;
};

// Example of a second, different shape: a flat plateau with small bumps.
class PlateauShape : public TerrainShape
{
public:
    PlateauShape(const WorldNoise& n, float height, float bump) : TerrainShape(n), h(height), b(bump) {}
    ColumnInfo Column(int x, int z, const Climate&) const override
    {
        return { h + noise.height.GetNoise(float(x), float(z)) * b, 1.0f };
    }
    float Density(const ColumnInfo& col, int, int y, int) const override
    {
        return col.baseHeight - float(y);        // solid below the surface
    }
private:
    float h, b;
};