#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 3) in uint aInstanceIndex;

struct InstanceData
{
    mat4 transform;
    uint meshID;
    uint materialID;
    uint animOffset; // unused here - static meshes don't animate
    uint _pad0;
};
layout(std430, binding = 0) readonly buffer InstanceDataBuffer
{
    InstanceData instanceData[];
};

uniform mat4 viewProjection;

out vec3 vWorldNormal;
out vec2 vUV;
flat out uint vMaterialID;

void main()
{
    InstanceData instance = instanceData[aInstanceIndex];

    vec4 worldPos = instance.transform * vec4(aPosition, 1.0);
    gl_Position = viewProjection * worldPos;

    // Computed per-vertex so non-uniform scale still shades correctly.
    // If this shows up in profiling, precompute it CPU-side at upload
    // time and add a mat3 to InstanceData instead of re-deriving it here
    // for every vertex.
    mat3 normalMatrix = transpose(inverse(mat3(instance.transform)));
    vWorldNormal = normalize(normalMatrix * aNormal);

    vUV = aUV;
    vMaterialID = instance.materialID;
}