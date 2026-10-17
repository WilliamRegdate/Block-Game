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

// Must match the horizon colours in sky.frag so distant terrain blends into the sky.
const vec3 kDayHorizon   = vec3(0.68, 0.82, 0.96);
const vec3 kNightHorizon = vec3(0.04, 0.06, 0.13);

const float kAmbientDay   = 0.45;   // brightness of faces turned away from the sun, by day
const float kAmbientNight = 0.15;   // ... and at night

void main()
{
    uint tri   = vFirstIndex / 3u + uint(gl_PrimitiveID);
    uint layer = triTex[tri];

    // Same day/night value as the sky: 0 = night, 1 = day
    float day = smoothstep(-0.12, 0.20, uSunDir.y);

    // Sun lighting: a scalar from the ambient floor up to 1.0, so it can only darken
    vec3  n       = normalize(vNormal);
    float diffuse = max(dot(n, uSunDir), 0.0) * day;
    float ambient = mix(kAmbientNight, kAmbientDay, day);
    float shade   = ambient + (1.0 - ambient) * diffuse;

    vec4 albedo = texture(materialTextures, vec3(vUV, float(layer)));
    vec3 color  = albedo.rgb * vLight.rgb * vLight.a * shade;

    // Optional distance fog toward the sky's horizon colour
    if (uFogEnd > 0.0)
    {
        float dist = length(vWorldPos - uCameraPos);
        float fog  = smoothstep(uFogEnd * 0.4, uFogEnd, dist);
        color = mix(color, mix(kNightHorizon, kDayHorizon, day), fog);
    }

    fragColor = vec4(color, albedo.a);
}
