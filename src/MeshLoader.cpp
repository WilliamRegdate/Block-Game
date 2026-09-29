#include "MeshLoader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <functional>

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

// Truncated to 32 bits to match Mesh::ID's width. Small collision risk in a
// very large asset set, but fine for now - flagged rather than hidden.
uint32_t HashPath(const char* path)
{
    return static_cast<uint32_t>(std::hash<std::string>{}(path));
}

} // namespace 


MeshLoader::MeshLoader() {}


// ============================================================
// Load
// ============================================================

Mesh* MeshLoader::Load(const char* filePath)
{
    if (!fileExists(filePath))
    {
        std::cerr << "Main glTF file not found: " << filePath << std::endl;
        return nullptr;
    }

    file = filePath;
    JSON = json::parse(get_file_contents(filePath));
    data = getData();

    // glTF puts a top-level "skins" array in the file whenever any skin
    // exists - simpler and more reliable than scanning every node for a
    // "skin" property individually.
    const bool skinned = JSON.contains("skins") && !JSON["skins"].empty();

    std::vector<GLuint> indices;
    Mesh* mesh = nullptr;

    if (skinned)
    {
        std::vector<SkinnedVertex> vertices;
        traverseNode(0, vertices, indices);
        mesh = new SkinnedMesh(vertices, indices);
        // TODO(skeleton): populate the SkinnedMesh's skeleton once Skeleton
        // holds real joint data - it's a stub for now, so there's nothing
        // to fill in yet.
    }
    else
    {
        std::vector<StaticVertex> vertices;
        traverseNode(0, vertices, indices);
        mesh = new StaticMesh(vertices, indices);
    }

    mesh->ID = HashPath(filePath);
    // TODO: no material loading yet - Material.h has no load path, so this
    // stays unset until that exists.

    return mesh;
}


// ============================================================
// Node traversal
// ============================================================

glm::mat4 MeshLoader::GetNodeLocalMatrix(const json& node)
{
    if (node.find("matrix") != node.end())
    {
        float matValues[16];
        for (unsigned int i = 0; i < node["matrix"].size(); i++)
            matValues[i] = node["matrix"][i];
        return glm::make_mat4(matValues);
    }

    glm::vec3 translation(0.0f);
    if (node.find("translation") != node.end())
    {
        float t[3];
        for (unsigned int i = 0; i < node["translation"].size(); i++)
            t[i] = node["translation"][i];
        translation = glm::make_vec3(t);
    }

    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.find("rotation") != node.end())
    {
        // glTF stores quaternions as [x, y, z, w]; glm::make_quat wants [w, x, y, z].
        float r[4] = { node["rotation"][3], node["rotation"][0], node["rotation"][1], node["rotation"][2] };
        rotation = glm::make_quat(r);
    }

    glm::vec3 scale(1.0f);
    if (node.find("scale") != node.end())
    {
        float s[3];
        for (unsigned int i = 0; i < node["scale"].size(); i++)
            s[i] = node["scale"][i];
        scale = glm::make_vec3(s);
    }

    const glm::mat4 trans = glm::translate(glm::mat4(1.0f), translation);
    const glm::mat4 rot   = glm::mat4_cast(rotation);
    const glm::mat4 sca   = glm::scale(glm::mat4(1.0f), scale);

    return trans * rot * sca;
}

void MeshLoader::traverseNode(unsigned int nextNode, std::vector<StaticVertex>& vertices, std::vector<GLuint>& indices, glm::mat4 matrix)
{
    json node = JSON["nodes"][nextNode];
    const glm::mat4 matNextNode = matrix * GetNodeLocalMatrix(node);

    if (node.find("mesh") != node.end())
        loadStaticMesh(node["mesh"], vertices, indices, matNextNode);

    if (node.find("children") != node.end())
        for (unsigned int i = 0; i < node["children"].size(); i++)
            traverseNode(node["children"][i], vertices, indices, matNextNode);
}

void MeshLoader::traverseNode(unsigned int nextNode, std::vector<SkinnedVertex>& vertices, std::vector<GLuint>& indices, glm::mat4 matrix)
{
    json node = JSON["nodes"][nextNode];
    const glm::mat4 matNextNode = matrix * GetNodeLocalMatrix(node);

    if (node.find("mesh") != node.end())
        loadSkinnedMesh(node["mesh"], vertices, indices, matNextNode);

    if (node.find("children") != node.end())
        for (unsigned int i = 0; i < node["children"].size(); i++)
            traverseNode(node["children"][i], vertices, indices, matNextNode);
}


// ============================================================
// Per-mesh loading
// ============================================================

void MeshLoader::loadStaticMesh(unsigned int indMesh, std::vector<StaticVertex>& vertices, std::vector<GLuint>& indices, const glm::mat4& nodeTransform)
{
    json primitive = JSON["meshes"][indMesh]["primitives"][0];
    json attributes = primitive["attributes"];

    std::vector<float> posVec = getFloats(JSON["accessors"][(unsigned int)attributes["POSITION"]]);
    std::vector<glm::vec3> positions = groupFloatsVec3(posVec);
    std::vector<float> normalVec = getFloats(JSON["accessors"][(unsigned int)attributes["NORMAL"]]);
    std::vector<glm::vec3> normals = groupFloatsVec3(normalVec);
    std::vector<float> texVec = getFloats(JSON["accessors"][(unsigned int)attributes["TEXCOORD_0"]]);
    std::vector<glm::vec2> texUVs = groupFloatsVec2(texVec);

    // Bake this node's placement into the vertices now, since every node's
    // contribution ends up combined into one mesh-local vertex buffer.
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(nodeTransform)));
    for (auto& p : positions) p = glm::vec3(nodeTransform * glm::vec4(p, 1.0f));
    for (auto& n : normals)   n = glm::normalize(normalMatrix * n);

    std::vector<StaticVertex> tempVertices = assembleStaticVertices(positions, normals, texUVs);

    // Indices from the accessor are relative to this primitive's own vertex
    // array (0-based). `vertices` may already hold earlier primitives'
    // data, so every index needs shifting by how many vertices are already
    // in there - the old code computed this offset but never applied it.
    const GLuint indexOffset = static_cast<GLuint>(vertices.size());
    std::vector<GLuint> tempIndices = getIndices(JSON["accessors"][(unsigned int)primitive["indices"]]);
    for (auto& index : tempIndices)
        indices.push_back(index + indexOffset);

    vertices.insert(vertices.end(), tempVertices.begin(), tempVertices.end());
}

void MeshLoader::loadSkinnedMesh(unsigned int indMesh, std::vector<SkinnedVertex>& vertices, std::vector<GLuint>& indices, const glm::mat4& nodeTransform)
{
    json primitive = JSON["meshes"][indMesh]["primitives"][0];
    json attributes = primitive["attributes"];

    std::vector<float> posVec = getFloats(JSON["accessors"][(unsigned int)attributes["POSITION"]]);
    std::vector<glm::vec3> positions = groupFloatsVec3(posVec);
    std::vector<float> normalVec = getFloats(JSON["accessors"][(unsigned int)attributes["NORMAL"]]);
    std::vector<glm::vec3> normals = groupFloatsVec3(normalVec);
    std::vector<float> texVec = getFloats(JSON["accessors"][(unsigned int)attributes["TEXCOORD_0"]]);
    std::vector<glm::vec2> texUVs = groupFloatsVec2(texVec);

    std::vector<glm::uvec4> joints;
    std::vector<glm::vec4>  weights;
    if (attributes.contains("JOINTS_0") && attributes.contains("WEIGHTS_0"))
    {
        joints = getJoints(JSON["accessors"][(unsigned int)attributes["JOINTS_0"]]);
        // TODO: assumes WEIGHTS_0 is FLOAT. glTF also allows normalized
        // UNSIGNED_BYTE/UNSIGNED_SHORT here; add that to getFloats (or a
        // parallel reader) if you hit an exporter that uses it.
        std::vector<float> weightVec = getFloats(JSON["accessors"][(unsigned int)attributes["WEIGHTS_0"]]);
        weights = groupFloatsVec4(weightVec);
    }
    else
    {
        std::cerr << "Skinned mesh primitive missing JOINTS_0/WEIGHTS_0: " << file << std::endl;
        joints.assign(positions.size(), glm::uvec4(0));
        weights.assign(positions.size(), glm::vec4(0.0f));
    }

    // Same reasoning as loadStaticMesh: this is the node's own placement,
    // separate from - and applied before - any runtime bone skinning.
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(nodeTransform)));
    for (auto& p : positions) p = glm::vec3(nodeTransform * glm::vec4(p, 1.0f));
    for (auto& n : normals)   n = glm::normalize(normalMatrix * n);

    std::vector<SkinnedVertex> tempVertices = assembleSkinnedVertices(positions, normals, texUVs, joints, weights);

    const GLuint indexOffset = static_cast<GLuint>(vertices.size());
    std::vector<GLuint> tempIndices = getIndices(JSON["accessors"][(unsigned int)primitive["indices"]]);
    for (auto& index : tempIndices)
        indices.push_back(index + indexOffset);

    vertices.insert(vertices.end(), tempVertices.begin(), tempVertices.end());
}


// ============================================================
// Vertex assembly
// ============================================================

std::vector<StaticVertex> MeshLoader::assembleStaticVertices
(
    std::vector<glm::vec3> positions,
    std::vector<glm::vec3> normals,
    std::vector<glm::vec2> texUVs
)
{
    std::vector<StaticVertex> vertices;
    vertices.reserve(positions.size());
    for (size_t i = 0; i < positions.size(); i++)
        vertices.push_back(StaticVertex{ positions[i], normals[i], texUVs[i] });
    return vertices;
}

std::vector<SkinnedVertex> MeshLoader::assembleSkinnedVertices
(
    std::vector<glm::vec3>  positions,
    std::vector<glm::vec3>  normals,
    std::vector<glm::vec2>  texUVs,
    std::vector<glm::uvec4> boneIndices,
    std::vector<glm::vec4>  boneWeights
)
{
    std::vector<SkinnedVertex> vertices;
    vertices.reserve(positions.size());
    for (size_t i = 0; i < positions.size(); i++)
        vertices.push_back(SkinnedVertex{ positions[i], normals[i], texUVs[i], boneIndices[i], boneWeights[i] });
    return vertices;
}


// ============================================================
// Raw accessor reading
// ============================================================

std::vector<unsigned char> MeshLoader::getData()
{
    std::string uri = JSON["buffers"][0]["uri"];

    std::string fileStr = std::string(file);
    std::string fileDirectory = fileStr.substr(0, fileStr.find_last_of('/') + 1);
    const std::string binPath = fileDirectory + uri;

    if (!fileExists(binPath))
        std::cerr << ".bin file not found: " << binPath << std::endl;

    std::string bytesText = get_file_contents(binPath.c_str());
    return std::vector<unsigned char>(bytesText.begin(), bytesText.end());
}

std::vector<float> MeshLoader::getFloats(json accessor)
{
    std::vector<float> floatVec;

    unsigned int buffViewInd   = accessor.value("bufferView", 0);
    unsigned int count         = accessor["count"];
    unsigned int accByteOffset = accessor.value("byteOffset", 0);
    std::string  type          = accessor["type"];

    json bufferView = JSON["bufferViews"][buffViewInd];
    unsigned int byteOffset = bufferView["byteOffset"];

    unsigned int numPerVert;
    if      (type == "SCALAR") numPerVert = 1;
    else if (type == "VEC2")   numPerVert = 2;
    else if (type == "VEC3")   numPerVert = 3;
    else if (type == "VEC4")   numPerVert = 4;
    else throw std::invalid_argument("Type is invalid (not SCALAR, VEC2, VEC3, or VEC4)");

    const unsigned int beginningOfData = byteOffset + accByteOffset;
    const unsigned int floatCount = count * numPerVert;

    floatVec.reserve(floatCount);
    for (unsigned int i = 0; i < floatCount; i++)
    {
        const unsigned int byteIndex = beginningOfData + i * 4;
        unsigned char bytes[] = { data[byteIndex], data[byteIndex + 1], data[byteIndex + 2], data[byteIndex + 3] };
        float value;
        std::memcpy(&value, bytes, sizeof(float));
        floatVec.push_back(value);
    }

    return floatVec;
}

std::vector<GLuint> MeshLoader::getIndices(json accessor)
{
    std::vector<GLuint> indices;

    unsigned int buffViewInd   = accessor.value("bufferView", 0);
    unsigned int count         = accessor["count"];
    unsigned int accByteOffset = accessor.value("byteOffset", 0);
    unsigned int componentType = accessor["componentType"];

    json bufferView = JSON["bufferViews"][buffViewInd];
    unsigned int byteOffset = bufferView["byteOffset"];
    const unsigned int beginningOfData = byteOffset + accByteOffset;

    indices.reserve(count);

    if (componentType == 5125) // UNSIGNED_INT
    {
        for (unsigned int i = 0; i < count; i++)
        {
            const unsigned int byteIndex = beginningOfData + i * 4;
            unsigned char bytes[] = { data[byteIndex], data[byteIndex + 1], data[byteIndex + 2], data[byteIndex + 3] };
            unsigned int value;
            std::memcpy(&value, bytes, sizeof(unsigned int));
            indices.push_back(static_cast<GLuint>(value));
        }
    }
    else if (componentType == 5123) // UNSIGNED_SHORT
    {
        for (unsigned int i = 0; i < count; i++)
        {
            const unsigned int byteIndex = beginningOfData + i * 2;
            unsigned char bytes[] = { data[byteIndex], data[byteIndex + 1] };
            unsigned short value;
            std::memcpy(&value, bytes, sizeof(unsigned short));
            indices.push_back(static_cast<GLuint>(value));
        }
    }
    else if (componentType == 5122) // SHORT
    {
        for (unsigned int i = 0; i < count; i++)
        {
            const unsigned int byteIndex = beginningOfData + i * 2;
            unsigned char bytes[] = { data[byteIndex], data[byteIndex + 1] };
            short value;
            std::memcpy(&value, bytes, sizeof(short));
            indices.push_back(static_cast<GLuint>(value));
        }
    }
    else
    {
        throw std::invalid_argument("Unsupported index component type");
    }

    return indices;
}

std::vector<glm::uvec4> MeshLoader::getJoints(json accessor)
{
    std::vector<glm::uvec4> joints;

    unsigned int buffViewInd   = accessor.value("bufferView", 0);
    unsigned int count         = accessor["count"];
    unsigned int accByteOffset = accessor.value("byteOffset", 0);
    unsigned int componentType = accessor["componentType"];
    std::string  type          = accessor["type"];

    if (type != "VEC4")
        throw std::invalid_argument("JOINTS_0 accessor must be VEC4");

    json bufferView = JSON["bufferViews"][buffViewInd];
    unsigned int byteOffset = bufferView["byteOffset"];
    const unsigned int beginningOfData = byteOffset + accByteOffset;

    unsigned int componentSize = 0;
    if      (componentType == 5121) componentSize = 1; // UNSIGNED_BYTE
    else if (componentType == 5123) componentSize = 2; // UNSIGNED_SHORT
    else throw std::invalid_argument("Unsupported JOINTS_0 component type (expected UNSIGNED_BYTE or UNSIGNED_SHORT)");

    joints.reserve(count);
    for (unsigned int v = 0; v < count; v++)
    {
        glm::uvec4 joint(0);
        for (unsigned int c = 0; c < 4; c++)
        {
            const unsigned int byteIndex = beginningOfData + (v * 4 + c) * componentSize;
            unsigned int value = 0;
            if (componentSize == 1)
            {
                value = data[byteIndex];
            }
            else
            {
                unsigned char bytes[] = { data[byteIndex], data[byteIndex + 1] };
                unsigned short shortValue;
                std::memcpy(&shortValue, bytes, sizeof(unsigned short));
                value = shortValue;
            }
            joint[c] = value;
        }
        joints.push_back(joint);
    }

    return joints;
}


// ============================================================
// Misc
// ============================================================

std::vector<glm::vec2> MeshLoader::groupFloatsVec2(std::vector<float> floatVec)
{
    std::vector<glm::vec2> vectors;
    vectors.reserve(floatVec.size() / 2);
    for (size_t i = 0; i + 1 < floatVec.size(); i += 2)
        vectors.push_back(glm::vec2(floatVec[i], floatVec[i + 1]));
    return vectors;
}

std::vector<glm::vec3> MeshLoader::groupFloatsVec3(std::vector<float> floatVec)
{
    std::vector<glm::vec3> vectors;
    vectors.reserve(floatVec.size() / 3);
    for (size_t i = 0; i + 2 < floatVec.size(); i += 3)
        vectors.push_back(glm::vec3(floatVec[i], floatVec[i + 1], floatVec[i + 2]));
    return vectors;
}

std::vector<glm::vec4> MeshLoader::groupFloatsVec4(std::vector<float> floatVec)
{
    std::vector<glm::vec4> vectors;
    vectors.reserve(floatVec.size() / 4);
    for (size_t i = 0; i + 3 < floatVec.size(); i += 4)
        vectors.push_back(glm::vec4(floatVec[i], floatVec[i + 1], floatVec[i + 2], floatVec[i + 3]));
    return vectors;
}

bool MeshLoader::fileExists(const std::string& fileName)
{
    std::ifstream infile(fileName);
    return infile.good();
}