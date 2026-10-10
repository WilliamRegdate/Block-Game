#version 460 core

layout(location = 0) in uint normalLight;
layout(location = 1) in uint uvLight;
layout(location = 2) in uvec3 position;
layout(location = 3) in uvec3 inTint;
layout(location = 4) in uint aInstanceIndex;

struct ChunkInstanceData
{
    vec3 position;
    uint firstIndex;
    uint indexCount;
    uint _pad;                              
};

layout(std430, binding = 0) readonly buffer ChunkData     { ChunkInstanceData chunks[]; };
layout(std430, binding = 1) readonly buffer QuadMaterials { uint quadMat[]; };   // two 16-bit ids per uint

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vLight;
flat out uint vLayer;

void main()
{
    ChunkInstanceData chunk = chunks[aInstanceIndex];
    uint quad        = aInstanceIndex / 6;

    uint matId = (quadMat[quad]);

    vec3 localPos = position * 0.125;

    uint n = normalLight & 63u;
    ivec3 s = ivec3(n & 3u, (n >> 2u) & 3u, (n >> 4u) & 3u);
    vec3 normal = normalize(vec3(s - 3 * (s >> 1)));      // 0 -> 0, 1 -> +1, 2 -> -1

    // ---- uv: bits 0-6 of uvLight (3 bits x, 3 bits y, bit 6 spare) ----
    vUV = vec2(uvLight & 7u, (uvLight >> 3u) & 7u);       // 0..7, not normalized

    // ---- light strength: 2 bits from normalLight (6-7) + 1 bit from uvLight (7) ----
    uint strengthBits = (normalLight >> 6u) | (((uvLight >> 7u) & 1u) << 2u);
    float strength = float(strengthBits) / 7.0;           // 3 bits = 0..7

    // ---- tint ----
    vec3 tint = vec3(inTint) / 255.0;
    vLight = mix(vec3(1.0), tint, strength);

    vec3 worldPos = chunk.position + localPos;
    vWorldPos = worldPos;
    vNormal   = normal;

    gl_Position = uViewProj * vec4(worldPos, 1.0);
}