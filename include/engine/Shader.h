#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class Shader
{
    public:
        GLuint ID = 0;
        Shader(const char *vertexFile, const char *fragmentFile);

        void Activate();
        void Delete();

        void SetMat4(const char* name, const glm::mat4& value);
        void SetVec3(const char* name, const glm::vec3& value);
        void SetInt(const char* name, int value);
        void SetFloat(const char* name, float value);
};

#endif