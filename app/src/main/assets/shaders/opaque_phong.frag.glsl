#version 450
layout(location = 0) in vec3 fragWorldNormal;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 lightDir;       // xyz = direction (world space)
    vec4 lightColor;     // rgb = color, a = intensity
    vec4 ambientColor;   // rgb = ambient color
    vec4 materialColor;  // rgb = fixed material color
} ubo;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 N = normalize(fragWorldNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 L = normalize(-ubo.lightDir.xyz);

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb;

    // Diffuse (Lambertian)
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * ubo.lightColor.rgb * ubo.lightColor.a;

    vec3 lighting = ambient + diffuse;

    outColor = vec4(ubo.materialColor.rgb * lighting, 1.0);
}
