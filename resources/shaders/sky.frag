#version 460 core

in vec2 vNdc;
out vec4 fragColor;

uniform mat4  uInvViewProj;   // inverse(projection * view)
uniform vec3  uSunDir;        // unit vector pointing TOWARD the sun

// How gradual sunrise / sunset is, in terms of the sun's height (uSunDir.y, -1..1).
// The sky goes from night to day as the sun rises from kTwilightStart to kTwilightEnd.
// Wider range = longer, softer fade. The sun is at the horizon when y = 0.
const float kTwilightStart = -0.30;   // fully night below this (about 17 degrees under the horizon)
const float kTwilightEnd   =  0.40;   // fully day above this (about 24 degrees above it)
const float kDuskWidth     =  0.55;   // how far from the horizon the orange sunset tint reaches

uniform float uTime;          // seconds, for star twinkle

// ---- palette -------------------------------------------------------------
const vec3 kDayZenith     = vec3(0.18, 0.40, 0.85);
const vec3 kDayHorizon    = vec3(0.68, 0.82, 0.96);
const vec3 kNightZenith   = vec3(0.005, 0.008, 0.03);
const vec3 kNightHorizon  = vec3(0.04, 0.06, 0.13);
const vec3 kSunsetColor   = vec3(1.00, 0.42, 0.18);
const vec3 kSunColor      = vec3(1.00, 0.92, 0.75);

float hash13(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    // View ray for this pixel: unproject a near and a far point, take the difference.
    // This only needs inverse(viewProjection), no separate camera position.
    vec4 nearP = uInvViewProj * vec4(vNdc, -1.0, 1.0);
    vec4 farP  = uInvViewProj * vec4(vNdc,  1.0, 1.0);
    vec3 dir   = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);

    float sunY  = uSunDir.y;
    float day   = smoothstep(kTwilightStart, kTwilightEnd, sunY);               // 0 = night, 1 = day
    float dusk  = 1.0 - smoothstep(0.0, kDuskWidth, abs(sunY));      // 1 when the sun is at the horizon

    // ---- base gradient -----------------------------------------------------
    vec3 zenith  = mix(kNightZenith,  kDayZenith,  day);
    vec3 horizon = mix(kNightHorizon, kDayHorizon, day);

    float up  = clamp(dir.y, 0.0, 1.0);
    vec3  sky = mix(horizon, zenith, pow(up, 0.45));           // rises fast near the horizon

    // ---- sunrise / sunset glow on the side facing the sun -----------------
    // Below the horizon, repeat the horizon colour: use the horizontal direction for the glow
    vec3  flatDir = vec3(dir.x, 0.0, dir.z);
    float flatLen = length(flatDir);
    vec3  skyDir  = dir.y >= 0.0 ? dir
                  : (flatLen > 1e-4 ? flatDir / flatLen : vec3(1.0, 0.0, 0.0));

    float cosSun      = dot(dir, uSunDir);                         // real angle to the sun (for the disc)
    float horizonBand = pow(1.0 - up, 5.0);
    float sunSide     = pow(max(dot(skyDir, uSunDir), 0.0), 3.0);
    sky += kSunsetColor * dusk * horizonBand * (0.25 + 0.75 * sunSide);

    // ---- stars (night only, above the horizon) -----------------------------
    float night = 1.0 - day;
    if (night > 0.01 && dir.y > 0.0)
    {
        vec3  scaled = dir * 230.0;
        vec3  cell   = floor(scaled);
        float r      = hash13(cell);
        float star   = step(0.9965, r);
        float d      = length(fract(scaled) - 0.5);
        star *= 1.0 - smoothstep(0.0, 0.38, d);                  // round, not square
        star *= 0.65 + 0.35 * sin(uTime * 2.0 + r * 200.0);      // twinkle
        star *= smoothstep(0.0, 0.15, dir.y);                    // fade near the horizon
        sky += vec3(0.9, 0.95, 1.0) * star * night;
    }

    // ---- sun disc + glow ---------------------------------------------------
    float aboveHorizon = smoothstep(-0.05, 0.03, dir.y);
    float disc = smoothstep(0.99955, 0.99975, cosSun);
    float glow = pow(max(cosSun, 0.0), 220.0) * 0.55
               + pow(max(cosSun, 0.0), 18.0)  * 0.12;
    vec3 sunTint = mix(kSunColor, kSunsetColor, dusk * 0.7);
    sky += sunTint * (disc * 1.6 + glow) * aboveHorizon;

    // ---- dither to hide 8-bit banding in the gradient ----------------------
    sky += (hash12(gl_FragCoord.xy + fract(uTime)) - 0.5) / 255.0;

    fragColor = vec4(sky, 1.0);
}
