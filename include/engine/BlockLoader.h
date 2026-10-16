#ifndef BLOCKLOADER_H
#define BLOCKLOADER_H

#include <json/json.h>
#include "Block.h"

using json = nlohmann::json;

class BlockLoader
{
    public:
        BlockType Load(const char* filePath, std::vector<std::string>& textures);
    private:

};

#endif