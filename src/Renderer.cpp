#include "RenderTypes.h"

// ============================================================
// GPUBuffer
// ============================================================

GPUBuffer::GPUBuffer(GLsizeiptr capacityBytes, GLenum target, GLenum usage)
    : m_target(target)
{
    glGenBuffers(1, &ID);
    glBindBuffer(m_target, ID);
    glBufferData(m_target, capacityBytes, nullptr, usage);
    glBindBuffer(m_target, 0);
}

void GPUBuffer::Upload(GLintptr offsetBytes, const void* data, GLsizeiptr sizeBytes)
{
    glBindBuffer(m_target, ID);
    glBufferSubData(m_target, offsetBytes, sizeBytes, data);
    glBindBuffer(m_target, 0);
}

void GPUBuffer::Delete()
{
    glDeleteBuffers(1, &ID);
    ID = 0;
}


// ============================================================
// VBO
// ============================================================

void VBO::Bind()
{
    glBindBuffer(GL_ARRAY_BUFFER, ID);
}

void VBO::Unbind()
{
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


// ============================================================
// EBO
// ============================================================

void EBO::Bind()
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ID);
}

void EBO::Unbind()
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}


// ============================================================
// SSBO
// ============================================================

void SSBO::BindBase(GLuint bindingIndex)
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingIndex, ID);
}


// ============================================================
// IndirectBuffer
// ============================================================

void IndirectBuffer::Bind()
{
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ID);
}

void IndirectBuffer::Unbind()
{
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}


// ============================================================
// VAO
// ============================================================

VAO::VAO()
{
    glGenVertexArrays(1, &ID);
}

void VAO::LinkAttrib(GPUBuffer& buffer, GLuint layout, GLuint numComponents,
                      GLenum type, GLsizeiptr stride, void* offset,
                      GLuint divisor)
{
    // Self-contained rather than assuming the caller already bound
    // this VAO - a deliberate change from the original tutorial
    // pattern this was adapted from. Fine here since it's only
    // called a handful of times at setup, never per frame.
    Bind();
    glBindBuffer(GL_ARRAY_BUFFER, buffer.ID);

    // Integer types (the live-index lookup value, bone indices)
    // need the "I" variant - plain glVertexAttribPointer would
    // reinterpret/normalize the bits instead of passing them
    // through as true integers.
    const bool isIntegerType =
        (type == GL_INT || type == GL_UNSIGNED_INT ||
         type == GL_SHORT || type == GL_UNSIGNED_SHORT ||
         type == GL_BYTE || type == GL_UNSIGNED_BYTE);

    if (isIntegerType)
    {
        glVertexAttribIPointer(layout, numComponents, type, (GLsizei)stride, offset);
    }
    else
    {
        glVertexAttribPointer(layout, numComponents, type, GL_FALSE, (GLsizei)stride, offset);
    }

    glEnableVertexAttribArray(layout);

    if (divisor > 0)
    {
        glVertexAttribDivisor(layout, divisor);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    Unbind();
}

void VAO::Bind()
{
    glBindVertexArray(ID);
}

void VAO::Unbind()
{
    glBindVertexArray(0);
}

void VAO::Delete()
{
    glDeleteVertexArrays(1, &ID);
    ID = 0;
}