#ifndef AABB_H
#define AABB_H

#include <glm/glm.hpp>
#include <variant>

struct AABB
{
    glm::vec3 min;
    glm::vec3 max;

    // True if `other` lies completely inside this box (touching the surface counts).
    bool contains(const AABB& other) const
    {
        return other.min.x >= min.x && other.max.x <= max.x &&
               other.min.y >= min.y && other.max.y <= max.y &&
               other.min.z >= min.z && other.max.z <= max.z;
    }

    // True if the two boxes overlap. They must overlap on all three axes.
    bool intersects(const AABB& other) const
    {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    //point test
    bool contains(const glm::vec3& p) const
    {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }

    glm::vec3 minimumOverlap(const AABB& other) const
    {
        if (!intersects(other)) return glm::vec3(0.0f);

        // Signed push distance on each axis, towards whichever side is closer.
        auto axis = [](float aMin, float aMax, float bMin, float bMax)
        {
            const float pushNeg = bMin - aMax;   // move this box towards -axis (<= 0)
            const float pushPos = bMax - aMin;   // move this box towards +axis (>= 0)
            return (-pushNeg < pushPos) ? pushNeg : pushPos;
        };

        const glm::vec3 d(axis(min.x, max.x, other.min.x, other.max.x),
                        axis(min.y, max.y, other.min.y, other.max.y),
                        axis(min.z, max.z, other.min.z, other.max.z));

        // Resolve along the axis with the smallest penetration.
        const glm::vec3 a = glm::abs(d);
        if (a.x <= a.y && a.x <= a.z) return glm::vec3(d.x, 0.0f, 0.0f);
        if (a.y <= a.z)               return glm::vec3(0.0f, d.y, 0.0f);
        return glm::vec3(0.0f, 0.0f, d.z);
    }

};


#endif