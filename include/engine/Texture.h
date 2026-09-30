#ifndef TEXTURE_CLASS_H
#define TEXTURE_CLASS_H

#include<glad/glad.h>
#include<stb/stb_image.h>
#include<cstdint>
#include<string>

class Texture
{
    public:
        std::string filepath;

        Texture(const char* path);

        // Loads the image from disk right now - nothing is cached on the
        // Texture itself. Always requests 4 channels (forced RGBA) so
        // every material texture matches TextureArray::UploadLayer's
        // default GL_RGBA format, regardless of the source file's actual
        // channel count. Returns nullptr (and logs) if the load fails.
        // Caller must pass the result to FreeImage() once done with it.
        unsigned char* GetImage(int& width, int& height);

        static void FreeImage(unsigned char* image);
};
#endif