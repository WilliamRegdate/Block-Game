#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 boneIndices;
layout(location = 4) in vec4 boneWeights;
layout(location = 5) in uint aInstanceIndex;

struct InstanceData
{
    mat4 transform;
    uint meshID;
    uint materialID;
    float animTime;
    uint animationID;
};

struct AnimationDescriptor
{
    uint poseStartIndex;
    uint frameCount;
    uint boneCount;
    float duration;
};

layout(std430, binding = 0) readonly buffer InstanceDataBuffer
{
    InstanceData instanceData[];
};

layout(std430, binding = 2) readonly buffer AnimationDescriptorBuffer
{
    AnimationDescriptor animationDescriptor[];
};

layout(std430, binding = 3) readonly buffer PoseBuffer
{
    mat4 poses[];
};

uniform mat4 viewProjection;

out vec3 vWorldNormal;
out vec2 vUV;
flat out uint vMaterialID;

void main()
{
    InstanceData instance = instanceData[aInstanceIndex];
    AnimationDescriptor animation = animationDescriptor[instance.animationID];

    float timePerFrame = animation.duration / float(animation.frameCount - 1);

    float framePosition = instance.animTime / timePerFrame;

    uint currentFrame = uint(framePosition);

    uint nextFrame =  min(currentFrame + 1, animation.frameCount - 1);

    float blend = fract(framePosition);

    uint currentFrameStart = animation.poseStartIndex + currentFrame * animation.boneCount;

    uint nextFrameStart = animation.poseStartIndex + nextFrame * animation.boneCount;

    mat4 currentSkinMatrix = poses[currentFrameStart + boneIndices.x] * boneWeights.x
        + poses[currentFrameStart + boneIndices.y] * boneWeights.y
        + poses[currentFrameStart + boneIndices.z] * boneWeights.z
        + poses[currentFrameStart + boneIndices.w] * boneWeights.w;

    mat4 nextSkinMatrix = poses[nextFrameStart + boneIndices.x] * boneWeights.x
    + poses[nextFrameStart + boneIndices.y] * boneWeights.y
    + poses[nextFrameStart + boneIndices.z] * boneWeights.z
    + poses[nextFrameStart + boneIndices.w] * boneWeights.w;

    mat4 skinMatrix =
    currentSkinMatrix * (1.0 - blend) +
    nextSkinMatrix * blend;

    vec4 skinnedPosition =
        skinMatrix * vec4(aPosition, 1.0);

    vec3 currentNormal =
      mat3(poses[currentFrameStart + boneIndices.x]) * aNormal * boneWeights.x
    + mat3(poses[currentFrameStart + boneIndices.y]) * aNormal * boneWeights.y
    + mat3(poses[currentFrameStart + boneIndices.z]) * aNormal * boneWeights.z
    + mat3(poses[currentFrameStart + boneIndices.w]) * aNormal * boneWeights.w;

    vec3 nextNormal =
        mat3(poses[nextFrameStart + boneIndices.x]) * aNormal * boneWeights.x
        + mat3(poses[nextFrameStart + boneIndices.y]) * aNormal * boneWeights.y
        + mat3(poses[nextFrameStart + boneIndices.z]) * aNormal * boneWeights.z
        + mat3(poses[nextFrameStart + boneIndices.w]) * aNormal * boneWeights.w;

    vec3 skinnedNormal =
        normalize(currentNormal * (1.0 - blend) + nextNormal * blend);

    vec4 worldPos =
        instance.transform * skinnedPosition;

    gl_Position =
        viewProjection * worldPos;

    mat3 normalMatrix =
        transpose(inverse(mat3(instance.transform)));

    vWorldNormal =
        normalize(normalMatrix * skinnedNormal);

    vUV = aUV;
    vMaterialID = instance.materialID;
}
