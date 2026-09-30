#ifndef ENTITY_H
#define ENTITY_H

#include "Transform.h"
#include "Mesh.h"

class Entity
{
    public:
        uint32_t ID;
        Transform transform;
        Mesh* mesh = nullptr;
        Material* material = nullptr;
        
    
};

#endif