#version 430 core

// ============================================================
// Per-vertex attributes (from the shared static VBO)
// ============================================================
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

// ============================================================
// Per-instance attribute (divisor = 1)
// ============================================================
// The real slot into instanceData below, for this instance. Sourced from
// the culled visible-index buffer, not gl_InstanceID directly - binding it
// as a divisor-1 vertex attribute means the driver applies each draw's
// baseInstance automatically when fetching it, so this shader never
// touches baseInstance itself. See VAO::LinkAttrib's comment in
// RenderTypes.h for why.
layout(location = 3) in uint aInstanceIndex;

// ============================================================
// SSBOs
// ============================================================
// Mirrors RenderTypes.h::InstanceData exactly - no vec3s in that struct,
// so its std430 layout matches the CPU struct's raw bytes with no padding
// surprises (unlike MeshDescriptor's AABB - see the note where that's
// used, in the fragment shader's material lookup).
struct InstanceData
{
    mat4 transform;
    uint meshID;
    uint materialID;
    uint animOffset; // unused here - static meshes don't animate
    uint _pad0;
};
layout(std430, binding = 0) readonly buffer InstanceDataBuffer
{
    InstanceData instanceData[];
};

// ============================================================
// Uniforms
// ============================================================
uniform mat4 viewProjection;

// ============================================================
// Outputs to the fragment shader
// ============================================================
out vec3 vWorldNormal;
out vec2 vUV;
flat out uint vMaterialID;

void main()
{
    InstanceData instance = instanceData[aInstanceIndex];

    vec4 worldPos = instance.transform * vec4(aPosition, 1.0);
    gl_Position = viewProjection * worldPos;

    // Computed per-vertex so non-uniform scale still shades correctly.
    // If this shows up in profiling, precompute it CPU-side at upload
    // time and add a mat3 to InstanceData instead of re-deriving it here
    // for every vertex.
    mat3 normalMatrix = transpose(inverse(mat3(instance.transform)));
    vWorldNormal = normalize(normalMatrix * aNormal);

    vUV = aUV;
    vMaterialID = instance.materialID;
}