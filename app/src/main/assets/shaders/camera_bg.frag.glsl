#version 450
// Compile: glslangValidator -V camera_bg.frag.glsl -o camera_bg.frag.spv

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 1) uniform sampler2D cameraTexture;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(cameraTexture, fragUV);
}
