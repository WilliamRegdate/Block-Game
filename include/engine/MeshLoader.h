#ifndef MESHLOADER_H
#define MESHLOADER_H

#include <glad/glad.h>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <json/json.h>
#include "Mesh.h"
#include "Skeleton.h"
#include "Animation.h"

using json = nlohmann::json;

// A node's local transform as separate translation/rotation/scale, rather
// than GetNodeLocalMatrix's composed matrix - needed for Bone::rest* below,
// which store the components separately, not a matrix.
struct NodeTRS
{
    glm::vec3 translation{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f };
};

class MeshLoader
{
    public:
        MeshLoader();

        // Loads a whole glTF file as one combined Mesh, in the file's own
        // node hierarchy order with every node's transform baked into its
        // vertices. Returns a heap-allocated StaticMesh* or SkinnedMesh*
        // (as a Mesh*) depending on whether the file has any skins, or
        // nullptr if the file couldn't be opened. Caller takes ownership.
        //
        // Mesh::ID is a hash of `file`, so loading the same path twice
        // produces the same ID.
        Mesh* Load(const char* filePath);

        // Loads the skeleton from the file's first skin. glTF allows more
        // than one skin per file; only the first is used, since nothing on
        // Mesh/SkinnedMesh records which skin a mesh was bound to. Operates
        // on whatever Load() most recently parsed - call Load() on the
        // target file first, on this same MeshLoader instance. Returns an
        // empty Skeleton (bones.empty()) if the file has no skin.
        //
        // Load() already calls this automatically for skinned files and
        // stores the result on SkinnedMesh::skeleton - call this directly
        // yourself only if you need to reload/recompute it separately.
        Skeleton LoadSkeleton();

        // Loads one animation by index from the currently-loaded file.
        // Each channel's target node is mapped to a bone via
        // skeleton.nodeIndices; channels targeting a node that isn't one of
        // the skeleton's joints are skipped (not an error - could be a
        // camera, a mesh node, etc).
        AnimationClip LoadAnimationClip(unsigned int animationIndex, const Skeleton& skeleton);

        // Loads every animation in the currently-loaded file, in file order.
        std::vector<AnimationClip> LoadAnimationClips(const Skeleton& skeleton);

    private:
        // baked animation sample rate per second
        const float animationSampleRate = 8;
        const char* file = nullptr;
        std::vector<unsigned char> data;
        json JSON;

        // This node's own local transform (its "matrix" property if
        // present, else composed from translation/rotation/scale), with no
        // parent transform applied.
        glm::mat4 GetNodeLocalMatrix(const json& node);

        // Same as GetNodeLocalMatrix, but returns the translation/
        // rotation/scale separately instead of composing them into a
        // matrix. If the node uses "matrix" instead of T/R/S, that matrix
        // is decomposed back into components.
        NodeTRS GetNodeLocalTRS(const json& node);

        // Walks the node graph from `nextNode` down, accumulating each
        // node's local transform into `matrix`, appending every mesh found
        // into `vertices`/`indices`. One overload per vertex type - a file
        // is loaded as either fully static or fully skinned (see Load), so
        // only one of these actually gets called per file.
        void traverseNode(unsigned int nextNode, std::vector<StaticVertex>& vertices, std::vector<GLuint>& indices, glm::mat4 matrix = glm::mat4(1.0f));
        void traverseNode(unsigned int nextNode, std::vector<SkinnedVertex>& vertices, std::vector<GLuint>& indices, glm::mat4 matrix = glm::mat4(1.0f));

        std::vector<unsigned char> getData();
        std::vector<float> getFloats(json accessor);
        std::vector<GLuint> getIndices(json accessor);

        // JOINTS_0 is UNSIGNED_BYTE or UNSIGNED_SHORT, never FLOAT, so it
        // can't go through getFloats().
        std::vector<glm::uvec4> getJoints(json accessor);

        std::vector<StaticVertex> assembleStaticVertices
        (
            std::vector<glm::vec3> positions,
            std::vector<glm::vec3> normals,
            std::vector<glm::vec2> texUVs
        );
        std::vector<SkinnedVertex> assembleSkinnedVertices
        (
            std::vector<glm::vec3>  positions,
            std::vector<glm::vec3>  normals,
            std::vector<glm::vec2>  texUVs,
            std::vector<glm::uvec4> boneIndices,
            std::vector<glm::vec4>  boneWeights
        );

        // nodeTransform is the accumulated world transform of the node this
        // mesh was found on - applied to positions/normals before they're
        // appended, since every node's contribution ends up in one shared
        // mesh-local vertex buffer.
        void loadStaticMesh(unsigned int indMesh, std::vector<StaticVertex>& vertices, std::vector<GLuint>& indices, const glm::mat4& nodeTransform);
        void loadSkinnedMesh(unsigned int indMesh, std::vector<SkinnedVertex>& vertices, std::vector<GLuint>& indices, const glm::mat4& nodeTransform);

        std::vector<glm::vec2> groupFloatsVec2(std::vector<float> floatVec);
        std::vector<glm::vec3> groupFloatsVec3(std::vector<float> floatVec);
        std::vector<glm::vec4> groupFloatsVec4(std::vector<float> floatVec);

        bool fileExists(const std::string& fileName);
};

#endif