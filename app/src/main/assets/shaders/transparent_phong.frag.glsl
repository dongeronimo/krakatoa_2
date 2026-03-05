#version 450
// glslangValidator -V -g -Od transparent_phong.frag.glsl -o transparent_phong.frag.spv
layout(location = 0) in vec3 fragWorldNormal;
layout(location = 1) in vec2 fragUV;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 lightDir;       // xyz = direction (world space)
    vec4 lightColor;     // rgb = color, a = intensity (pixelIntensity)
    vec4 ambientColor;   // rgb = ambient color
} ubo;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(texSampler, fragUV);

    vec3 N = normalize(fragWorldNormal);
    vec3 L = normalize(-ubo.lightDir.xyz); // light dir points FROM source, negate for dot product

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb;

    // Diffuse (Lambertian)
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * ubo.lightColor.rgb * ubo.lightColor.a;

    vec3 lighting = ambient + diffuse;

    outColor = vec4(texColor.rgb * lighting, texColor.a);
}
