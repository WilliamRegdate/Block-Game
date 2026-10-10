#version 460 core

uniform sampler2DArray materialTextures;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vLight;
flat in uint vLayer;

out vec4 fragColour;

void main()
{
    vec4 albedo = texture(materialTextures, vec3(vUV, float(vLayer)));
    fragColour  = vec4(albedo.rgb * vLight, albedo.a);
}