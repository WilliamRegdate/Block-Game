#version 430 core

in vec3 vWorldNormal;
in vec2 vUV;
flat in uint vMaterialID;

out vec4 FragColor;

struct MaterialDescriptor
{
    uint  albedoTexIndex;
    uint  normalTexIndex;
    uint  ormTexIndex;
    uint  _pad0;
    vec4  tint;
};
layout(std430, binding = 1) readonly buffer MaterialDescriptorBuffer
{
    MaterialDescriptor materialDescriptors[];
};

layout(binding = 0) uniform sampler2DArray materialTextures;

const uint NO_TEXTURE = 0xFFFFFFFFu;

const vec3 kLightDir   = normalize(vec3(-0.4, -1.0, -0.3));
const vec3 kLightColor = vec3(1.0);
const vec3 kAmbient    = vec3(0.15);

void main()
{
    MaterialDescriptor mat = materialDescriptors[vMaterialID];

    vec4 albedo = mat.tint;
    if (mat.albedoTexIndex != NO_TEXTURE)
        albedo *= texture(materialTextures, vec3(vUV, float(mat.albedoTexIndex)));

    float occlusion = 1.0;
    if (mat.ormTexIndex != NO_TEXTURE)
        occlusion = texture(materialTextures, vec3(vUV, float(mat.ormTexIndex))).r;

    vec3 normal = normalize(vWorldNormal);

    float diffuse = max(dot(normal, -kLightDir), 0.0);
    vec3 lighting = kAmbient * occlusion + kLightColor * diffuse;

    FragColor = vec4(albedo.rgb * lighting, albedo.a);
}