#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Transform
{
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;

    glm::mat3x3 basis;

};

#endif