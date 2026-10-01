#include "Texture.h"

#include <iostream>

// NOTE: stb_image.h is a header-only library that needs its implementation
// generated exactly once somewhere in the project via:
//     #define STB_IMAGE_IMPLEMENTATION
//     #include <stb/stb_image.h>
// Not added here since I don't know whether another file in your project
// already does it - adding it twice causes duplicate-symbol linker errors.

Texture::Texture(const char* path)
    : filepath(path)
{
}

unsigned char* Texture::GetImage(int& width, int& height)
{
    int channels = 0;
    //stbi_set_flip_vertically_on_load(true);
    unsigned char* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, 4);

    if (pixels == nullptr)
        std::cerr << "Failed to load texture: " << filepath << std::endl;

    return pixels;
}

void Texture::FreeImage(unsigned char* image)
{
    stbi_image_free(image);
}