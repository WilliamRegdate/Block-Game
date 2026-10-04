#ifndef SKELETON_H
#define SKELETON_H

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <vector>


struct Bone
{
    std::string name;
    int32_t     parent = -1;                  // index into Skeleton::bones, -1 for a root
    glm::mat4   inverseBind{ 1.0f };          // from skin.inverseBindMatrices[i]

    // The node's own rest pose. Used whenever a clip has no track for this bone.
    glm::vec3   restTranslation{ 0.0f };
    glm::quat   restRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3   restScale{ 1.0f };
};

struct Skeleton
{
    std::vector<Bone>     bones;       // same order as skin.joints, so JOINTS_0 values index it directly
    std::vector<uint32_t> nodeIndices; // the glTF node index each bone came from, same order as bones -
                                        // needed to map an animation channel's target node back to a bone
                                        // index. Same length as bones.
    std::vector<uint32_t> evalOrder;   // bone indices with every parent before its children
};

#endif