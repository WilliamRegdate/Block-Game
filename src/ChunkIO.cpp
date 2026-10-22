#include "ChunkIO.h"
#include <unordered_map>
#include <vector>

fs::path ChunkIO::PathFor(const glm::ivec3& p)
{
    return fs::path(DIRECTORY) /
           (std::to_string(p.x) + "_" + std::to_string(p.y) + "_" + std::to_string(p.z) + ".chk");
}

std::optional<Chunk> ChunkIO::LoadChunk(const glm::ivec3& position)
{
    std::ifstream file(PathFor(position), std::ios::binary | std::ios::ate);
    if (!file)
        return std::nullopt;                        // no saved copy

    const std::streamsize size = file.tellg();
    if (size <= 0)
        return std::nullopt;
    file.seekg(0);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        return std::nullopt;

    Chunk chunk = Chunk(Voxel());                   // start from air
    if (!Deserialize(bytes.data(), bytes.size(), chunk))
        return std::nullopt;                        // corrupt: caller regenerates

    chunk.position = position;                      // not stored in the file
    return chunk;
}

bool ChunkIO::SaveChunk(const Chunk& chunk)
{
    std::vector<uint8_t> bytes;
    Serialize(chunk, bytes);

    const fs::path finalPath = PathFor(chunk.position);
    fs::path tempPath = finalPath;
    tempPath += ".tmp";

    std::lock_guard<std::mutex> lock(writeMutex);

    std::error_code ec;
    fs::create_directories(DIRECTORY, ec);          // ofstream won't create the folder

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!file) return false;
    }   // closed and flushed here

    fs::rename(tempPath, finalPath, ec);            // replaces the old file
    if (ec)
    {
        fs::remove(tempPath, ec);
        return false;
    }
    return true;
}

// ChunkIO.cpp

namespace
{
    constexpr int CELLS = Chunk::SIZE * Chunk::SIZE * Chunk::SIZE;

    // Voxel <-> u32. If Voxel ever gains fields, this is the one place to update.
    uint32_t Pack(const Voxel& v)
    {
        return uint32_t(v.voxelType) | (uint32_t(v.voxelLayer) << 8) | (uint32_t(v.ID) << 16);
    }

    Voxel Unpack(uint32_t k)
    {
        Voxel v;
        v.voxelType  = uint8_t(k & 0xFF);
        v.voxelLayer = uint8_t((k >> 8) & 0xFF);
        v.ID         = uint16_t(k >> 16);
        return v;
    }

    void Put16(std::vector<uint8_t>& o, uint16_t v)
    {
        o.push_back(uint8_t(v & 0xFF));
        o.push_back(uint8_t(v >> 8));
    }

    void Put32(std::vector<uint8_t>& o, uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            o.push_back(uint8_t((v >> (8 * i)) & 0xFF));
    }

    struct Reader
    {
        const uint8_t* p;
        const uint8_t* end;
        bool ok = true;

        uint8_t U8()
        {
            if (p >= end) { ok = false; return 0; }
            return *p++;
        }
        uint16_t U16()
        {
            const uint16_t a = U8();
            const uint16_t b = U8();
            return uint16_t(a | (b << 8));
        }
        uint32_t U32()
        {
            const uint32_t a = U16();
            const uint32_t b = U16();
            return a | (b << 16);
        }
    };
}

void ChunkIO::Serialize(const Chunk& chunk, std::vector<uint8_t>& out)
{
    std::vector<uint32_t>                  palette;   // palette index -> packed voxel
    std::unordered_map<uint32_t, uint16_t> lookup;    // packed voxel  -> palette index
    std::vector<std::pair<uint16_t, uint16_t>> runs;  // (palette index, length)

    for (int x = 0; x < Chunk::SIZE; ++x)
    for (int y = 0; y < Chunk::SIZE; ++y)
    for (int z = 0; z < Chunk::SIZE; ++z)
    {
        const uint32_t key = Pack(chunk.GetVoxel(x, y, z));

        auto [it, added] = lookup.try_emplace(key, uint16_t(palette.size()));
        if (added)
            palette.push_back(key);

        const uint16_t index = it->second;
        if (!runs.empty() && runs.back().first == index && runs.back().second < 0xFFFF)
            ++runs.back().second;
        else
            runs.push_back({ index, 1 });
    }

    out.reserve(out.size() + 12 + palette.size() * 4 + runs.size() * 4);

    Put32(out, MAGIC);
    Put16(out, VERSION);
    Put16(out, uint16_t(palette.size()));
    for (uint32_t key : palette)
        Put32(out, key);

    Put32(out, uint32_t(runs.size()));
    for (const auto& [index, length] : runs)
    {
        Put16(out, index);
        Put16(out, length);
    }
}

bool ChunkIO::Deserialize(const uint8_t* data, size_t size, Chunk& chunk)
{
    Reader r{ data, data + size };

    if (r.U32() != MAGIC)   return false;
    if (r.U16() != VERSION) return false;

    const uint16_t paletteCount = r.U16();
    if (!r.ok || paletteCount == 0) return false;

    std::vector<Voxel> palette;
    palette.reserve(paletteCount);
    for (uint16_t i = 0; i < paletteCount; ++i)
        palette.push_back(Unpack(r.U32()));
    if (!r.ok) return false;

    const uint32_t runCount = r.U32();
    if (!r.ok) return false;

    int cell = 0;
    for (uint32_t i = 0; i < runCount; ++i)
    {
        const uint16_t index  = r.U16();
        const uint16_t length = r.U16();
        if (!r.ok) return false;

        if (index >= palette.size())  return false;   // bad palette reference
        if (cell + length > CELLS)    return false;   // run overflows the chunk

        const Voxel& voxel = palette[index];
        for (int k = 0; k < length; ++k, ++cell)
        {
            const int x =  cell / (Chunk::SIZE * Chunk::SIZE);
            const int y = (cell / Chunk::SIZE) % Chunk::SIZE;
            const int z =  cell % Chunk::SIZE;
            chunk.SetVoxel(x, y, z, voxel);
        }
    }

    return cell == CELLS;   // runs must cover every voxel exactly
}