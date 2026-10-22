#version 460 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLight;
flat in uint vFirstIndex;

out vec4 fragColor;

layout(binding = 0) uniform sampler2DArray materialTextures;
layout(std430, binding = 1) readonly buffer TriMaterials { uint triTex[]; };  // one uint per triangle

uniform vec3  uSunDir;      // unit vector pointing TOWARD the sun (same value the sky uses)
uniform vec3  uCameraPos;   // only needed for fog
uniform float uFogEnd;      // distance where fog is fully opaque; 0 (the default) = fog off

// Sky palette: must match sky.frag so the fog blends into the sky behind it.
const vec3 kDayZenith    = vec3(0.18, 0.40, 0.85);
const vec3 kDayHorizon   = vec3(0.68, 0.82, 0.96);
const vec3 kNightZenith  = vec3(0.005, 0.008, 0.03);
const vec3 kNightHorizon = vec3(0.04, 0.06, 0.13);
const vec3 kSunsetColor  = vec3(1.00, 0.42, 0.18);

// The sky's gradient + sunset tint in a given direction (no stars or sun disc).
// Keep in sync with sky.frag.
vec3 SkyColor(vec3 dir)
{
    float day  = smoothstep(-0.12, 0.20, uSunDir.y);
    float dusk = 1.0 - smoothstep(0.0, 0.35, abs(uSunDir.y));

    vec3 zenith  = mix(kNightZenith,  kDayZenith,  day);
    vec3 horizon = mix(kNightHorizon, kDayHorizon, day);

    float up  = clamp(dir.y, 0.0, 1.0);   // clamped: terrain meets the sky at the horizon, not below it
    vec3  sky = mix(horizon, zenith, pow(up, 0.45));

    float horizonBand = pow(1.0 - up, 5.0);
    float sunSide     = pow(max(dot(dir, uSunDir), 0.0), 3.0);
    return sky + kSunsetColor * dusk * horizonBand * (0.25 + 0.75 * sunSide);
}

// ---- light colour ----------------------------------------------------------
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
    uint tri   = vFirstIndex / 3u + uint(gl_PrimitiveID);
    uint layer = triTex[tri];

    // Same day/night value as the sky: 0 = night, 1 = day
    float day = smoothstep(-0.12, 0.20, uSunDir.y);

    // Sun lighting: ambient floor + sun, each with its own colour
    vec3  n       = normalize(vNormal);
    float diffuse = max(dot(n, uSunDir), 0.0) * day;
    float ambient = mix(kAmbientNight, kAmbientDay, day);

    vec3 sunColor, ambientColor;
    LightColors(sunColor, ambientColor);
    vec3 light = ambientColor * ambient + sunColor * ((1.0 - ambient) * diffuse);

    vec4 albedo = texture(materialTextures, vec3(vUV, float(layer)));
    vec3 color  = albedo.rgb * vLight.rgb * vLight.a * light;

    // Optional distance fog: fades to the sky colour in this fragment's view direction
    if (uFogEnd > 0.0)
    {
        vec3  toFrag = vWorldPos - uCameraPos;
        float dist   = max(length(toFrag), 0.0001);
        float fog    = smoothstep(uFogEnd * 0.4, uFogEnd, dist);
        fog = pow(fog, 0.8);
        color = mix(color, SkyColor(toFrag / dist), fog);
    }

    fragColor = vec4(color, albedo.a);
}
