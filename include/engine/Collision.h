#ifndef COLLISION_H
#define COLLISION_H

#include "AABB.h"
#include "Entity.h"
#include "WorldManager.h"

//stores collision bodies

struct BlockRef  { glm::ivec3 pos; const uint32_t ID; };

// other in contact refers to the objects aabb
struct Contact
{
    AABB self;    // snapshot of the mover's box at detection time
    AABB other;   // snapshot of the thing it overlapped
    glm::vec3 normal;
    glm::vec3 position;
    std::variant<BlockRef, Entity*> target; // reference to either a block or a 
};
class CollisionDetector
{
    public:
        explicit CollisionDetector(WorldManager* world);

        std::vector<Contact> contactCache;
        bool GetContacts(Entity* entity); // outputs into the contact cache
        bool GetRay(glm::vec3 origin, glm::vec3 dir, float maxDist);; // outputs into the contact cache
    private:
        WorldManager* world;

};



#endif