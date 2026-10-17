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
    uint animOffset;
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

    mat3 normalMatrix = transpose(inverse(mat3(instance.transform)));
    vWorldNormal = normalize(normalMatrix * aNormal);

    vUV = aUV;
    vMaterialID = instance.materialID;
}