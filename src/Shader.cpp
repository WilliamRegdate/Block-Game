#include "Shader.h"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

// File-local: nothing outside this file needs get_file_contents, and giving
// it external linkage (as the original header declaration did) risks
// colliding with another file's helper of the same name - same reasoning as
// the copies in MeshLoader.cpp/MaterialLoader.cpp.
namespace
{

std::string get_file_contents(const char* filename)
{
    std::FILE* fp = std::fopen(filename, "rb");
    if (!fp)
        throw std::runtime_error(std::string("Could not open file: ") + filename);

    std::string contents;
    std::fseek(fp, 0, SEEK_END);
    contents.resize(static_cast<size_t>(std::ftell(fp)));
    std::rewind(fp);
    std::fread(&contents[0], 1, contents.size(), fp);
    std::fclose(fp);
    return contents;
}

bool CheckCompileStatus(GLuint shaderID, const char* filename)
{
    GLint compileStatus;
    glGetShaderiv(shaderID, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus != GL_TRUE)
    {
        GLint infoLogLength;
        glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &infoLogLength);
        if (infoLogLength <= 0)
        {
            std::cout << "Shader compile error (" << filename << "): (no log provided)" << std::endl;
            return false;
        }

        GLchar* buffer = new GLchar[infoLogLength];
        GLsizei bufferSize;
        glGetShaderInfoLog(shaderID, infoLogLength, &bufferSize, buffer);
        std::cout << "Shader compile error (" << filename << "): " << buffer << std::endl;
        delete[] buffer;
        return false;
    }
    return true;
}

bool CheckProgramStatus(GLuint programID)
{
    GLint linkStatus;
    glGetProgramiv(programID, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE)
    {
        GLint infoLogLength;
        glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &infoLogLength);
        if (infoLogLength <= 0)
        {
            std::cout << "Shader link error: (no log provided)" << std::endl;
            return false;
        }

        GLchar* buffer = new GLchar[infoLogLength];
        GLsizei bufferSize;
        glGetProgramInfoLog(programID, infoLogLength, &bufferSize, buffer);
        std::cout << "Shader link error: " << buffer << std::endl;
        delete[] buffer;
        return false;
    }
    return true;
}

} // namespace

Shader::Shader(const char* vertexFile, const char* fragmentFile)
{
    std::string vertexCode = get_file_contents(vertexFile);
    std::string fragmentCode = get_file_contents(fragmentFile);
    const char* vertexSource = vertexCode.c_str();
    const char* fragmentSource = fragmentCode.c_str();

    // Defines shaders, sets their source, then compiles them into machine code.
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, NULL);
    glCompileShader(vertexShader);
    CheckCompileStatus(vertexShader, vertexFile);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
    glCompileShader(fragmentShader);
    CheckCompileStatus(fragmentShader, fragmentFile);

    // Creates the shader program and attaches the shaders.
    ID = glCreateProgram();
    glAttachShader(ID, vertexShader);
    glAttachShader(ID, fragmentShader);
    glLinkProgram(ID);
    CheckProgramStatus(ID);

    // Deletes the individual shader objects - no longer needed once linked
    // into the program.
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

void Shader::Activate()
{
    glUseProgram(ID);
}

void Shader::Delete()
{
    glDeleteProgram(ID);
    ID = 0; // matches every other Delete() in this codebase (GPUBuffer, VAO, TextureArray)
}

void Shader::SetMat4(const char* name, const glm::mat4& value)
{
    GLint location = glGetUniformLocation(ID, name);
    glProgramUniformMatrix4fv(ID, location, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::SetInt(const char* name, int value)
{
    GLint location = glGetUniformLocation(ID, name);
    glProgramUniform1i(ID, location, value);
}

void Shader::SetFloat(const char* name, float value)
{
    GLint location = glGetUniformLocation(ID, name);
    glProgramUniform1f(ID, location, value);
}