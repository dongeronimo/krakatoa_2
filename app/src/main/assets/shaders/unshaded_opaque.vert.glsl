#version 450
// glslangValidator -V -g -Od .\unshaded_opaque.vert.glsl -o unshaded_opaque.vert.spv
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;   // não usado
layout(location = 2) in vec2 inUV;       // não usado

layout(set = 0, binding = 0) uniform UBO
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;
} ubo;

layout(location = 0) out vec4 fragColor;

void main()
{
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = ubo.color;
}
