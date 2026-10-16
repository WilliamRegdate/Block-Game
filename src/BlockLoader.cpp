#include "BlockLoader.h"

#include <fstream>
#include <iostream>

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
    bool fileExists(const std::string& fileName)
    {
        std::ifstream infile(fileName);
        return infile.good();
    }
}
BlockType BlockLoader::Load(const char *filePath, std::vector<std::string>& textures)
{
    BlockType block;
    if (!fileExists(filePath))
    {
        std::cerr << "Materials file not found: " << filePath << std::endl;
        return BlockType();
    }

    json JSON = json::parse(get_file_contents(filePath));

    if (!JSON.contains("id"))
    {
        std::cerr << "block file has no \"id\": " << filePath << std::endl;
        return BlockType();
    }

    block.id = JSON["id"];
    block.name = JSON["name"];
    textures.reserve(JSON["textureIDs"].size());
    for (const auto& entry : JSON["textureIDs"])
        textures.push_back(entry);
    block.tex_x_plus = JSON["x_plus"];
    block.tex_x_minus = JSON["x_minus"];
    block.tex_y_plus = JSON["y_plus"];
    block.tex_y_minus = JSON["y_minus"];
    block.tex_z_plus = JSON["z_plus"];
    block.tex_z_minus = JSON["z_minus"];

    return block;
}