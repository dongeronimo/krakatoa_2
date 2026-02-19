#version 450
// Compile: glslangValidator -V camera_bg.frag.glsl -o camera_bg.frag.spv

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 1) uniform sampler2D yTexture;   // R8  full-res
layout(set = 0, binding = 2) uniform sampler2D uvTexture;  // RG8 half-res

layout(location = 0) out vec4 outColor;

void main()
{
    float Y = texture(yTexture, fragUV).r;
    vec2  uv = texture(uvTexture, fragUV).rg;

    // NV12: R = U, G = V
    float U = uv.r - 0.5;
    float V = uv.g - 0.5;

    // BT.601 full-range YUV -> RGB
    float R = Y + 1.402 * V;
    float G = Y - 0.344 * U - 0.714 * V;
    float B = Y + 1.772 * U;

    outColor = vec4(R, G, B, 1.0);
}
