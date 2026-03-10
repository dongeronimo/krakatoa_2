#version 450
// glslangValidator -V -g -Od transparent_phong.vert.glsl -o transparent_phong.vert.spv
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;   // inverse-transpose of model
    vec4 lightDir;       // xyz = direction (world space), w = unused
    vec4 lightColor;     // rgb = color, a = intensity (pixelIntensity)
    vec4 ambientColor;   // rgb = ambient color, a = unused
} ubo;

layout(location = 0) out vec3 fragWorldNormal;
layout(location = 1) out vec2 fragUV;

void main()
{
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    // No Y-flip here: the compose pass already handles the OpenGL→Vulkan
    // Y convention difference via its UV flip (1.0 - inUV.y).
    fragWorldNormal = normalize(mat3(ubo.normalMatrix) * inNormal);
    fragUV = inUV;
}
