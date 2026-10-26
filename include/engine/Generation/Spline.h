#pragma once
#include <vector>

struct SplinePoint { float x; float y; };

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float SampleSpline(const std::vector<SplinePoint>& pts, float x)
{
    if (x <= pts.front().x) return pts.front().y;
    if (x >= pts.back().x)  return pts.back().y;
    for (size_t i = 1; i < pts.size(); ++i)
    {
        if (x <= pts[i].x)
        {
            const SplinePoint& a = pts[i - 1];
            const SplinePoint& b = pts[i];
            return Lerp(a.y, b.y, (x - a.x) / (b.x - a.x));
        }
    }
    return pts.back().y;
}