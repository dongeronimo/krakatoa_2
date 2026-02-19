#version 450
// Compile: glslangValidator -V camera_bg.vert.glsl -o camera_bg.vert.spv

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;   // unused
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform UBO {
    // Display rotation from Android (0 = 0, 1 = 90, 2 = 180, 3 = 270).
    // The camera image is always landscape; this rotates the UVs so the
    // image is correctly oriented for the current device orientation.
    int displayRotation;
} ubo;

layout(location = 0) out vec2 fragUV;

void main()
{
    // Fullscreen quad: position is already in NDC [-1,1], depth at max
    gl_Position = vec4(inPosition.xy, 1.0, 1.0);

    // Rotate UV around center (0.5, 0.5) based on display rotation.
    // Camera sensor is landscape; we rotate to match screen orientation.
    vec2 uv = inUV;
    vec2 centered = uv - 0.5;
    vec2 rotated;

    // Android Surface.ROTATION_* values:
    //  0 = portrait (camera needs 90 CW rotation)
    //  1 = landscape (no rotation needed)
    //  2 = reverse portrait (camera needs 270 CW rotation)
    //  3 = reverse landscape (camera needs 180 rotation)
    if (ubo.displayRotation == 0) {
        // 90 degrees CW: (x,y) -> (y, -x)
        rotated = vec2(centered.y, -centered.x);
    } else if (ubo.displayRotation == 1) {
        // no rotation
        rotated = centered;
    } else if (ubo.displayRotation == 2) {
        // 270 degrees CW (= 90 CCW): (x,y) -> (-y, x)
        rotated = vec2(-centered.y, centered.x);
    } else {
        // 180 degrees: (x,y) -> (-x, -y)
        rotated = vec2(-centered.x, -centered.y);
    }

    fragUV = rotated + 0.5;
}
