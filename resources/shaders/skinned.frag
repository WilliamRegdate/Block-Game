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

uniform vec3 uSunDir;   // unit vector pointing TOWARD the sun (same value the sky and chunks use)

// ---- light colour ----------------------------------------------------------
const vec3  kSunsetColor   = vec3(1.00, 0.42, 0.18);
const vec3  kSunNoonColor  = vec3(1.00, 0.96, 0.88);   // direct sun at midday
const vec3  kSkyFillColor  = vec3(0.85, 0.92, 1.00);   // ambient fill by day (sky blue)
const vec3  kMoonColor     = vec3(0.55, 0.65, 1.00);   // ambient at night
const float kTintStrength  = 0.8;                      // 0 = white light, 1 = full tint

// Colour of the direct sun light and of the ambient fill for the current time of day.
// Both stay <= 1 per channel, so lighting can only darken the texture, never blow it out.
void LightColors(out vec3 sunColor, out vec3 ambientColor)
{
    float day  = smoothstep(-0.12, 0.20, uSunDir.y);
    float dusk = 1.0 - smoothstep(0.0, 0.35, abs(uSunDir.y));   // 1 at sunrise / sunset

    // Direct sun: warm white at noon, orange when it's low
    sunColor = mix(kSunNoonColor, kSunsetColor, dusk * 0.85);

    // Ambient: sky-blue by day, moonlight blue at night, a little warmer at dusk
    ambientColor = mix(kMoonColor, kSkyFillColor, day);
    ambientColor = mix(ambientColor, kSunsetColor, dusk * 0.3 * day);

    sunColor     = mix(vec3(1.0), sunColor,     kTintStrength);
    ambientColor = mix(vec3(1.0), ambientColor, kTintStrength);
}

const float kAmbientDay   = 0.45;   // brightness of faces turned away from the sun, by day
const float kAmbientNight = 0.15;   // ... and at night

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

    // Same day/night value as the sky and chunk shaders: 0 = night, 1 = day
    float day     = smoothstep(-0.12, 0.20, uSunDir.y);
    float ambient = mix(kAmbientNight, kAmbientDay, day);
    float diffuse = max(dot(normal, uSunDir), 0.0) * day;

    vec3 sunColor, ambientColor;
    LightColors(sunColor, ambientColor);

    // Ambient occlusion only dims the ambient part; direct sun is unaffected
    vec3 light = ambientColor * (ambient * occlusion) + sunColor * ((1.0 - ambient) * diffuse);

    FragColor = vec4(albedo.rgb * light, albedo.a);
}
