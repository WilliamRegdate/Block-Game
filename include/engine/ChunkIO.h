#ifndef CHUNKIO_H
#define CHUNKIO_H

#include "Chunk.h"

//responsible for saving and loading chunks to and from storage
class ChunkIO
{
    public:
        //loads a chunk from storage
        Chunk LoadChunk(const glm::ivec3& position);
        //saves a chunk to storage
        void SaveChunk(const Chunk& chunk);
    private:
        //puts chunks into the saved storage format
        void Serialize(const Chunk& chunk, std::vector<uint8_t>& out);
        // loads a chunk object from data from storage
        bool Deserialize(const uint8_t* data, size_t size, Chunk& chunk);
        static constexpr uint32_t MAGIC = 0x4B484342;
        static constexpr uint16_t VERSION = 1;
};


#endif