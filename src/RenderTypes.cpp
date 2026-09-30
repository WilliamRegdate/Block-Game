#include "RenderTypes.h"
#include "Material.h"
#include "Texture.h"

#include <algorithm>
#include <cmath>
#include <iostream>

// ============================================================
// GPUBuffer
// ============================================================

GPUBuffer::GPUBuffer(GLsizeiptr capacityBytes, GLenum target, GLenum usage)
    : m_target(target)
{
    glGenBuffers(1, &ID);
    glBindBuffer(GL_COPY_WRITE_BUFFER, ID);
    glBufferData(GL_COPY_WRITE_BUFFER, capacityBytes, nullptr, usage);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void GPUBuffer::Upload(GLintptr offsetBytes, const void* data, GLsizeiptr sizeBytes)
{
    glBindBuffer(GL_COPY_WRITE_BUFFER, ID);
    glBufferSubData(GL_COPY_WRITE_BUFFER, offsetBytes, sizeBytes, data);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
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


// ============================================================
// TextureArray
// ============================================================

TextureArray::TextureArray(GLsizei width, GLsizei height, GLsizei layerCount, GLenum internalFormat)
    : m_width(width), m_height(height), m_layerCount(layerCount)
{
    // Immutable storage (glTexStorage3D, GL 4.2+) needs the mip level
    // count fixed up front - without allocating room for them here,
    // GenerateMipmaps() would have nowhere to write beyond level 0.
    const GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(width, height)))) + 1;

    glGenTextures(1, &ID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, ID);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, internalFormat, width, height, layerCount);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void TextureArray::UploadLayer(GLint layer, const void* pixelData, GLenum format, GLenum type)
{
    glBindTexture(GL_TEXTURE_2D_ARRAY, ID);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, m_width, m_height, 1, format, type, pixelData);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void TextureArray::UploadMaterial(Material* material, GLint albedoLayer, GLint normalLayer, GLint ormLayer)
{
    // Loads, uploads and immediately frees each texture's CPU-side pixel
    // data - per "the CPU doesn't need a permanent store of textures",
    // nothing here is kept around after the upload.
    auto uploadOne = [this](std::optional<Texture>& tex, GLint layer)
    {
        if (!tex.has_value() || layer < 0)
            return;

        int width = 0, height = 0;
        unsigned char* pixels = tex->GetImage(width, height);
        if (pixels == nullptr)
            return; // Texture::GetImage already logged the failure

        if (width != m_width || height != m_height)
        {
            std::cerr << "Texture size (" << width << "x" << height << ") doesn't match "
                      << "material TextureArray size (" << m_width << "x" << m_height << "): "
                      << tex->filepath << std::endl;
            Texture::FreeImage(pixels);
            return;
        }

        UploadLayer(layer, pixels);
        Texture::FreeImage(pixels);
    };

    uploadOne(material->albedo, albedoLayer);
    uploadOne(material->normal, normalLayer);
    uploadOne(material->orm,    ormLayer);
}

void TextureArray::GenerateMipmaps()
{
    glBindTexture(GL_TEXTURE_2D_ARRAY, ID);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void TextureArray::Bind(GLuint unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, ID);
}

void TextureArray::Delete()
{
    glDeleteTextures(1, &ID);
    ID = 0;
}