#version 430 core

in vec3 vWorldNormal;
in vec2 vUV;
flat in uint vMaterialID;

out vec4 FragColor;

// Mirrors RenderTypes.h::MaterialDescriptor exactly - same reasoning as
// InstanceData in the vertex shader, no vec3s so std430 lines up with the
// CPU struct's raw bytes.
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

// The shared material texture array - every material's albedo/normal/orm
// textures live here as layers. Bound once per pipeline, not per material.
// This is a texture-unit binding, a separate namespace from the SSBO
// `binding` numbers above - 0 doesn't collide with InstanceDataBuffer's 0.
layout(binding = 0) uniform sampler2DArray materialTextures;

// Sentinel for "this material has no such map" - matches
// BufferRange::start's default / INVALID_HANDLE_VALUE in RenderTypes.h.
// Relies on the not-yet-written material-upload code writing this exact
// value into MaterialDescriptor for any map a material doesn't have
// (i.e. writing handle.albedoLayer.start etc. straight through, since
// AllocateMaterial already leaves an unused BufferRange at its default).
const uint NO_TEXTURE = 0xFFFFFFFFu;

// Simple placeholder lighting - one directional light plus ambient. Swap
// for whatever your actual lighting model ends up being.
const vec3 kLightDir   = normalize(vec3(-0.4, -1.0, -0.3));
const vec3 kLightColor = vec3(1.0);
const vec3 kAmbient    = vec3(0.15);

void main()
{
    MaterialDescriptor mat = materialDescriptors[vMaterialID];

    vec4 albedo = mat.tint;
    if (mat.albedoTexIndex != NO_TEXTURE)
        albedo *= texture(materialTextures, vec3(vUV, float(mat.albedoTexIndex)));

    // ORM is occlusion/roughness/metallic packed in R/G/B (the usual
    // glTF convention). Only occlusion is used below, as a simple
    // ambient multiplier - this isn't doing real PBR roughness/metallic
    // shading, just enough to make the ORM data do something visible.
    float occlusion = 1.0;
    if (mat.ormTexIndex != NO_TEXTURE)
        occlusion = texture(materialTextures, vec3(vUV, float(mat.ormTexIndex))).r;

    // NOTE: normal mapping is NOT applied even though mat.normalTexIndex
    // exists - StaticVertex has no tangent attribute, so there's no
    // tangent space to map a normal map into. This uses the plain
    // interpolated vertex normal. Add a tangent (and likely a bitangent,
    // or derive one in-shader from UV derivatives) to StaticVertex if you
    // want real normal mapping later.
    vec3 normal = normalize(vWorldNormal);

    float diffuse = max(dot(normal, -kLightDir), 0.0);
    vec3 lighting = kAmbient * occlusion + kLightColor * diffuse;

    FragColor = vec4(albedo.rgb * lighting, albedo.a);
}