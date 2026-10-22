#ifndef CHUNKIO_H
#define CHUNKIO_H

#include <optional>
#include <filesystem>
#include <mutex>
#include <fstream>
#include "Chunk.h"


namespace fs = std::filesystem;
//responsible for saving and loading chunks to and from storage
class ChunkIO
{
    public:
        ChunkIO() = delete;   // static-only

        static std::optional<Chunk> LoadChunk(const glm::ivec3& position);
        // file name comes from chunk.position; false on any IO failure
        static bool SaveChunk(const Chunk& chunk);

    private:
        static std::filesystem::path PathFor(const glm::ivec3& position);
        static void Serialize(const Chunk& chunk, std::vector<uint8_t>& out);
        static bool Deserialize(const uint8_t* data, size_t size, Chunk& chunk);

        static constexpr uint32_t MAGIC   = 0x4B484342;
        static constexpr uint16_t VERSION = 1;
        static constexpr const char* DIRECTORY = "world/chunks";
        static inline std::mutex writeMutex;   // serialises saves
};


#endif