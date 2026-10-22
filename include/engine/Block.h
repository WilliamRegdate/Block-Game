#ifndef BLOCK_H
#define BLOCK_H

#include <string>

struct BlockType
{
    uint32_t id;
    std::string name;
    
    uint32_t tex_x_plus;
    uint32_t tex_x_minus;
    uint32_t tex_y_plus;
    uint32_t tex_y_minus;
    uint32_t tex_z_plus;
    uint32_t tex_z_minus;
};


#endif