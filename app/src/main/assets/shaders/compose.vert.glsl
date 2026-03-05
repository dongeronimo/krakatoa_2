#version 450
// Compile: glslangValidator -V compose.vert.glsl -o compose.vert.spv

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;   // unused
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

void main()
{
    gl_Position = vec4(inPosition.xy, 1.0, 1.0);
    // Flip V for Vulkan (Y-down in framebuffer, UV origin top-left)
    fragUV = vec2(inUV.x, 1.0 - inUV.y);
}
