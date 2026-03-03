#version 450
// glslangValidator -V offscreen_composite.frag.glsl -o offscreen_composite.frag.spv

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D offscreenTexture;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(offscreenTexture, fragUV);
}
