#ifndef VISUALINSTANCE_H
#define VISUALINSTANCE_H

#include "Transform.h"
#include "Mesh.h"
#include "AnimationPlayer.h"

class VisualInstance
{
    public:
        uint32_t ID;
        Transform* transform = nullptr;
        Mesh* mesh = nullptr;
        Material* material = nullptr;
        AnimationPlayer animationPlayer;
};

#endif