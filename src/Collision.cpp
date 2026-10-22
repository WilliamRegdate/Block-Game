#include "Collision.h"

CollisionDetector::CollisionDetector(WorldManager *world)
    : world(world)
{
}

bool CollisionDetector::GetContacts(Entity *entity)
{
    //clear the contact cache before doing a thing
    contactCache.clear();
    return false;
}

//TODO needs better method of getting collision shape only gets cube currently
bool CollisionDetector::GetRay(glm::vec3 origin, glm::vec3 dir, float maxDist)
{
    contactCache.clear();
    if (glm::dot(dir, dir) == 0.0f) return false;
    dir = glm::normalize(dir);

    glm::ivec3 cell = glm::ivec3(glm::floor(origin));

    glm::ivec3 step;
    glm::vec3  tMax, tDelta;
    const float INF = std::numeric_limits<float>::infinity();

    for (int i = 0; i < 3; ++i)
    {
        if (dir[i] > 0.0f) {
            step[i]   = 1;
            tDelta[i] = 1.0f / dir[i];
            tMax[i]   = (cell[i] + 1 - origin[i]) / dir[i];
        } else if (dir[i] < 0.0f) {
            step[i]   = -1;
            tDelta[i] = -1.0f / dir[i];
            tMax[i]   = (cell[i] - origin[i]) / dir[i];
        } else {
            step[i]   = 0;
            tDelta[i] = INF;
            tMax[i]   = INF;
        }
    }

    float     t = 0.0f;
    glm::vec3 normal(0.0f);          // stays zero if the origin starts inside a block

    while (t <= maxDist)
    {
        const Voxel& voxel = world->GetBlockAt(cell);

        if (!voxel.IsAir())
        {
            const glm::vec3 hitPoint = origin + dir * t;   // lies on the entered face

            Contact c = {
                { hitPoint, hitPoint },
                { glm::vec3(cell), glm::vec3(cell) + 1.0f },
                normal,
                hitPoint,
                BlockRef{ cell, voxel.ID }
            };
            contactCache.push_back(c);
            return true;
        }

        int axis = 0;
        if (tMax.y < tMax[axis]) axis = 1;
        if (tMax.z < tMax[axis]) axis = 2;

        cell[axis] += step[axis];
        t           = tMax[axis];
        tMax[axis] += tDelta[axis];

        normal       = glm::vec3(0.0f);
        normal[axis] = static_cast<float>(-step[axis]);    // face we just came through
    }
    return false;
}
