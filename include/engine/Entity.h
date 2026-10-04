#ifndef ENTITY_H
#define ENTITY_H

#include "Transform.h"
#include "Mesh.h"
#include "AnimationPlayer.h"

class Entity
{
    public:
        uint32_t ID;
        Transform transform;
        Mesh* mesh = nullptr;
        Material* material = nullptr;
        AnimationPlayer animationPlayer;
};

#endif