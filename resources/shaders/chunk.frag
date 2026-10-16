#version 460 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLight;
flat in uint vFirstIndex;

out vec4 fragColor;

layout(binding = 0) uniform sampler2DArray materialTextures;
layout(std430, binding = 1) readonly buffer TriMaterials { uint triTex[]; };  // one uint per triangle

const vec3 kLightDir   = normalize(vec3(-0.4, -1.0, -0.3));
const vec3 kLightColor = vec3(1.0);

void main()
{
    uint tri   = vFirstIndex / 3u + uint(gl_PrimitiveID);
    uint layer = triTex[tri];

    float diffuse = max(dot(vNormal, -kLightDir), 0.0);
    vec3 lighting = vLight.a*0.5 + kLightColor * diffuse;

    vec4 albedo = texture(materialTextures, vec3(vUV, float(layer)));
    fragColor   = vec4(albedo.rgb * vLight.rgb * vLight.a , albedo.a);
}