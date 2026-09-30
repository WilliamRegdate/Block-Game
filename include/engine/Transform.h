#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Transform
{
    public:
        glm::vec3 position{ 0.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // identity (w, x, y, z)
        glm::vec3 scale{ 1.0f };

        glm::mat4 GetMatrix() const
        {
            return glm::translate(glm::mat4(1.0f), position)
                 * glm::mat4_cast(rotation)
                 * glm::scale(glm::mat4(1.0f), scale);
        }
    private:
       // glm::mat3x3 basis;
};

#endif