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

layout(std430, binding = 0) readonly buffer ChunkData { ChunkInstanceData chunks[]; };

uniform mat4 viewProjection;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLight;
flat out uint vFirstIndex;

void main()
{
    ChunkInstanceData chunk = chunks[aInstanceIndex];
    vFirstIndex = chunk.firstIndex;

    vec3 localPos = vec3(position) * 0.25;

    uint n = normalLight & 63u;
    ivec3 s = ivec3(n & 3u, (n >> 2u) & 3u, (n >> 4u) & 3u);
    vec3 normal = normalize(vec3(s - 3 * (s >> 1)));

    vUV = vec2(uvLight & 7u, (uvLight >> 3u) & 7u);

    uint strengthBits = (normalLight >> 6u)
                  | (((uvLight >> 7u) & 1u) << 2u)
                  | (((uvLight >> 6u) & 1u) << 3u);
    float strength = float(strengthBits) / 15.0;

    vec3 tint = vec3(inTint) / 255.0;
    vLight = vec4(tint, strength);

    vec3 worldPos = (chunk.position * 32.0) + localPos;
    vWorldPos = worldPos;
    vNormal   = normal;

    gl_Position = viewProjection * vec4(worldPos, 1.0);
}