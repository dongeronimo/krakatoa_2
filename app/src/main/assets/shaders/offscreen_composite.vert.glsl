#version 450
// glslangValidator -V offscreen_composite.vert.glsl -o offscreen_composite.vert.spv

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;   // unused
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

void main()
{
    gl_Position = vec4(inPosition.xy, 1.0, 1.0);
    fragUV = inUV;
}
