#version 460 core

// Fullscreen triangle, no vertex buffer needed: draw 3 vertices.
out vec2 vNdc;

void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); // (0,0) (2,0) (0,2)
    vNdc = p * 2.0 - 1.0;                                                  // (-1,-1) (3,-1) (-1,3)
    gl_Position = vec4(vNdc, 1.0, 1.0);                                    // on the far plane
}
