#ifndef ENTITY_H
#define ENTITY_H

#include "VisualInstance.h"

class Entity
{
public:
    Entity();
    Entity(const Entity& o) : instance(o.instance), transform(o.transform) { instance.transform = &transform; }
    Entity(Entity&& o) noexcept : instance(std::move(o.instance)), transform(o.transform) { instance.transform = &transform; }
    Entity& operator=(const Entity& o) { instance = o.instance; transform = o.transform; instance.transform = &transform; return *this; }
    Entity& operator=(Entity&& o) noexcept { instance = std::move(o.instance); transform = o.transform; instance.transform = &transform; return *this; }

    VisualInstance instance;
    Transform transform;
};

#endif