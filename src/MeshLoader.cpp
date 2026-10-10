#include "MeshLoader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <glm/gtx/matrix_decompose.hpp>

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

// Topological sort: every parent before its children. Bones are usually
// already in that order (skin.joints almost always lists parents before
// children), but this guarantees it rather than assuming. Iterative DFS
// (not recursive) so a very deep bone chain can't blow the stack.
std::vector<uint32_t> ComputeEvalOrder(const Skeleton& skeleton)
{
    const size_t boneCount = skeleton.bones.size();
    std::vector<std::vector<uint32_t>> children(boneCount);
    std::vector<uint32_t> roots;

    for (uint32_t i = 0; i < boneCount; i++)
    {
        if (skeleton.bones[i].parent < 0)
            roots.push_back(i);
        else
            children[skeleton.bones[i].parent].push_back(i);
    }

    std::vector<uint32_t> order;
    order.reserve(boneCount);

    std::vector<uint32_t> stack(roots.rbegin(), roots.rend());
    while (!stack.empty())
    {
        uint32_t bone = stack.back();
        stack.pop_back();
        order.push_back(bone);
        for (auto it = children[bone].rbegin(); it != children[bone].rend(); ++it)
            stack.push_back(*it);
    }

    return order;
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
        const unsigned int sceneIndex = JSON["scene"];
        const json& sceneNodes = JSON["scenes"][sceneIndex]["nodes"];

        for (const auto& node : sceneNodes)
            traverseNode(static_cast<unsigned int>(node), vertices, indices);

        auto* skinnedMesh = new SkinnedMesh(vertices, indices);

        // The skeleton is only needed to bake, so it stays a local.
        const Skeleton skeleton = LoadSkeleton();
        if (!skeleton.bones.empty())
        {
            for (const AnimationClip& clip : LoadAnimationClips(skeleton))
                skinnedMesh->animations.push_back(BakeAnimationClip(skeleton, clip, animationSampleRate)); // 8fps
        }

        mesh = skinnedMesh;
    }
    else
    {
        std::vector<StaticVertex> vertices;
        const unsigned int sceneIndex = JSON["scene"];
        const json& sceneNodes = JSON["scenes"][sceneIndex]["nodes"];

        for (const auto& node : sceneNodes)
        {
            traverseNode(
                static_cast<unsigned int>(node),
                vertices,
                indices
            );
        }
        mesh = new StaticMesh(vertices, indices);
    }

    mesh->ID = HashPath(filePath);

    return mesh;
}


// ============================================================
// Skeleton
// ============================================================

Skeleton MeshLoader::LoadSkeleton()
{
    Skeleton skeleton; // bones stays empty if returned early below - that's the "no skin found" signal

    if (!JSON.contains("skins") || JSON["skins"].empty())
    {
        std::cerr << "No skin in glTF file: " << file << std::endl;
        return skeleton;
    }

    // TODO: only the first skin is loaded. glTF allows multiple skins per
    // file, but nothing on Mesh/SkinnedMesh records which skin a mesh was
    // bound to, so there's currently no way to pick a different one.
    const json& skin = JSON["skins"][0];
    const json& joints = skin["joints"]; // array of node indices, in skeleton order
    const size_t boneCount = joints.size();

    skeleton.bones.resize(boneCount);
    skeleton.nodeIndices.resize(boneCount);

    // inverseBindMatrices is optional per spec (absent means identity for
    // every joint) - an accessor of 4x4 floats, one matrix per joint, same
    // order as `joints`.
    std::vector<glm::mat4> inverseBindMatrices;
    if (skin.contains("inverseBindMatrices"))
    {
        std::vector<float> flat = getFloats(JSON["accessors"][(unsigned int)skin["inverseBindMatrices"]]);
        inverseBindMatrices.reserve(boneCount);
        for (size_t i = 0; i < boneCount; i++)
            inverseBindMatrices.push_back(glm::make_mat4(&flat[i * 16]));
    }

    // Node index -> bone index, so each joint's parent *node* can be turned
    // into a parent *bone* index below.
    std::unordered_map<uint32_t, uint32_t> nodeToBone;
    for (size_t i = 0; i < boneCount; i++)
        nodeToBone[(uint32_t)joints[i]] = (uint32_t)i;

    // One pass over every node's children, recording each child's parent -
    // O(node count) total, rather than re-scanning all nodes per joint.
    std::unordered_map<uint32_t, uint32_t> nodeParent;
    for (unsigned int n = 0; n < JSON["nodes"].size(); n++)
    {
        const json& candidate = JSON["nodes"][n];
        if (candidate.find("children") == candidate.end())
            continue;
        for (const auto& child : candidate["children"])
            nodeParent[(uint32_t)child] = n;
    }

    for (size_t i = 0; i < boneCount; i++)
    {
        const uint32_t nodeIndex = joints[i];
        const json& node = JSON["nodes"][nodeIndex];

        Bone& bone = skeleton.bones[i];
        bone.name = node.value("name", std::string("bone_") + std::to_string(i));
        bone.inverseBind = i < inverseBindMatrices.size() ? inverseBindMatrices[i] : glm::mat4(1.0f);

        NodeTRS trs = GetNodeLocalTRS(node);
        bone.restTranslation = trs.translation;
        bone.restRotation    = trs.rotation;
        bone.restScale       = trs.scale;

        // -1 (root) unless this joint's parent node is itself a joint.
        bone.parent = -1;
        auto parentNodeIt = nodeParent.find(nodeIndex);
        if (parentNodeIt != nodeParent.end())
        {
            auto parentBoneIt = nodeToBone.find(parentNodeIt->second);
            if (parentBoneIt != nodeToBone.end())
                bone.parent = static_cast<int32_t>(parentBoneIt->second);
        }

        skeleton.nodeIndices[i] = nodeIndex;
    }

    skeleton.evalOrder = ComputeEvalOrder(skeleton);

    return skeleton;
}


// ============================================================
// Animation
// ============================================================

AnimationClip MeshLoader::LoadAnimationClip(unsigned int animationIndex, const Skeleton& skeleton)
{
    AnimationClip clip;

    if (!JSON.contains("animations") || animationIndex >= JSON["animations"].size())
    {
        std::cerr << "Animation index " << animationIndex << " out of range in: " << file << std::endl;
        return clip;
    }

    const json& animation = JSON["animations"][animationIndex];
    clip.name = animation.value("name", std::string("animation_") + std::to_string(animationIndex));
    clip.ID = HashPath((file + clip.name).c_str());
    clip.bones.resize(skeleton.bones.size());

    // Node index -> bone index, same reasoning as LoadSkeleton: channels
    // target node indices, but tracks are stored by bone index.
    std::unordered_map<uint32_t, uint32_t> nodeToBone;
    for (size_t i = 0; i < skeleton.nodeIndices.size(); i++)
        nodeToBone[skeleton.nodeIndices[i]] = (uint32_t)i;

    const json& samplers = animation["samplers"];

    for (const auto& channel : animation["channels"])
    {
        const uint32_t targetNode = channel["target"]["node"];
        const std::string path = channel["target"]["path"];

        auto boneIt = nodeToBone.find(targetNode);
        if (boneIt == nodeToBone.end())
            continue; // targets a node that isn't one of this skeleton's joints

        const uint32_t boneIndex = boneIt->second;
        const json& sampler = samplers[(unsigned int)channel["sampler"]];

        std::vector<float> times = getFloats(JSON["accessors"][(unsigned int)sampler["input"]]);

        Interpolation interp = Interpolation::Linear;
        const std::string interpStr = sampler.value("interpolation", std::string("LINEAR"));
        if (interpStr == "STEP") interp = Interpolation::Step;
        else if (interpStr == "CUBICSPLINE") interp = Interpolation::CubicSpline;
        // Note: for CUBICSPLINE, the accessor's own declared count already
        // covers the 3-values-per-key layout (in-tangent/value/out-tangent),
        // so `values` naturally ends up 3x `times` below with no special
        // casing needed here - Track<T>'s consumer is what needs to know
        // to interpret it that way.

        if (path == "translation" || path == "scale")
        {
            std::vector<float> flat = getFloats(JSON["accessors"][(unsigned int)sampler["output"]]);
            std::vector<glm::vec3> values = groupFloatsVec3(flat);

            Track<glm::vec3>& track = (path == "translation") ? clip.bones[boneIndex].translation : clip.bones[boneIndex].scale;
            track.times = times;
            track.values = values;
            track.mode = interp;
        }
        else if (path == "rotation")
        {
            std::vector<float> flat = getFloats(JSON["accessors"][(unsigned int)sampler["output"]]);
            std::vector<glm::quat> values;
            values.reserve(flat.size() / 4);
            for (size_t i = 0; i + 3 < flat.size(); i += 4)
                // glTF quaternions are [x,y,z,w]; glm::quat's constructor is (w,x,y,z).
                values.emplace_back(flat[i + 3], flat[i], flat[i + 1], flat[i + 2]);

            Track<glm::quat>& track = clip.bones[boneIndex].rotation;
            track.times = times;
            track.values = values;
            track.mode = interp;
        }
        else
        {
            // "weights" (morph targets) - not handled, this loader is
            // skeletal-animation only.
            continue;
        }

        if (!times.empty())
            clip.duration = std::max(clip.duration, times.back());
    }

    return clip;
}

std::vector<AnimationClip> MeshLoader::LoadAnimationClips(const Skeleton& skeleton)
{
    std::vector<AnimationClip> clips;

    if (!JSON.contains("animations"))
        return clips;

    clips.reserve(JSON["animations"].size());
    for (unsigned int i = 0; i < JSON["animations"].size(); i++)
        clips.push_back(LoadAnimationClip(i, skeleton));

    return clips;
}


// ============================================================
// Node traversal
// ============================================================

NodeTRS MeshLoader::GetNodeLocalTRS(const json& node)
{
    NodeTRS trs;

    if (node.find("matrix") != node.end())
    {
        // Decompose the explicit matrix back into components - Bone::rest*
        // needs them separately, not a composed matrix.
        float matValues[16];
        for (unsigned int i = 0; i < node["matrix"].size(); i++)
            matValues[i] = node["matrix"][i];
        glm::mat4 m = glm::make_mat4(matValues);

        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(m, trs.scale, trs.rotation, trs.translation, skew, perspective);
        // glm::decompose returns the conjugate of the rotation glm::mat4_cast
        // expects back - a well-known GLM quirk. Worth a visual sanity check
        // once there's a skinned mesh actually rendering.
        trs.rotation = glm::conjugate(trs.rotation);

        return trs;
    }

    if (node.find("translation") != node.end())
    {
        float t[3];
        for (unsigned int i = 0; i < node["translation"].size(); i++)
            t[i] = node["translation"][i];
        trs.translation = glm::make_vec3(t);
    }

    if (node.find("rotation") != node.end())
    {
        // glTF stores quaternions as [x, y, z, w]; glm::make_quat wants [w, x, y, z].
        float r[4] = { node["rotation"][3], node["rotation"][0], node["rotation"][1], node["rotation"][2] };
        trs.rotation = glm::make_quat(r);
    }

    if (node.find("scale") != node.end())
    {
        float s[3];
        for (unsigned int i = 0; i < node["scale"].size(); i++)
            s[i] = node["scale"][i];
        trs.scale = glm::make_vec3(s);
    }

    return trs;
}

glm::mat4 MeshLoader::GetNodeLocalMatrix(const json& node)
{
    if (node.find("matrix") != node.end())
    {
        float matValues[16];
        for (unsigned int i = 0; i < node["matrix"].size(); i++)
            matValues[i] = node["matrix"][i];
        return glm::make_mat4(matValues);
    }

    NodeTRS trs = GetNodeLocalTRS(node);
    const glm::mat4 trans = glm::translate(glm::mat4(1.0f), trs.translation);
    const glm::mat4 rot   = glm::mat4_cast(trs.rotation);
    const glm::mat4 sca   = glm::scale(glm::mat4(1.0f), trs.scale);

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
    else if (type == "MAT4")   numPerVert = 16;
    else
        throw std::invalid_argument("Type is invalid (not SCALAR, VEC2, VEC3, VEC4, or MAT4): " + type);


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