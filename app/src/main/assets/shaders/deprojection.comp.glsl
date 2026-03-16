#version 450
// Takes the AR depth buffer (from ArFrame_acquireDepthImage16Bits) and deprojects
// its depth values to world-space positions.

// Buffer 0: camera intrinsics
layout (set = 0, binding = 0) buffer UBO {
    float fx; // Focal length in px
    float fy; // Focal length in px
    float cx; // Principal point
    float cy; // Principal point
} ubo;

// Buffer 1: depth data as packed uint32 (two uint16 depth values per element)
layout(set = 0, binding = 1) buffer DepthBuffer {
    uint data[];
} depthBuffer;

// Buffer 2: output world positions
layout(set = 0, binding = 2) buffer OutputBuffer {
    vec4 positions[];
} outBuffer;

layout (push_constant) uniform Dimensions {
    mat4 viewInverse; //I need that to go from camera space to world space
    uint width;
    uint height;
} pc;
layout(local_size_x = 16, local_size_y = 16) in;

void main() {
    uvec2 coord = gl_GlobalInvocationID.xy;
    uint u = coord.x;
    uint v = coord.y;

    // Bounds check
    if (u >= pc.width || v >= pc.height) return;
    // Mind that because there will be a point for each pixel the output buffer should have
    // a size = width * height.
    uint index = v * pc.width + u;

    // Read uint16 depth from packed uint32 array (ARCore uint16 is in millimeters)
    uint packed = depthBuffer.data[index >> 1u];
    uint raw16  = (index & 1u) == 0u ? (packed & 0xFFFFu) : (packed >> 16u);
    float depth = float(raw16) / 1000.0;

    if (depth <= 0.0) {
        outBuffer.positions[index] = vec4(0.0);
        return;
    }

    // Pixel → camera space
    float x_cam = (float(u) - ubo.cx) / ubo.fx * depth;
    float y_cam = (float(v) - ubo.cy) / ubo.fy * depth;
    float z_cam = -depth;

    vec4 p_cam = vec4(x_cam, y_cam, z_cam, 1.0);
    // Camera space → world space
    outBuffer.positions[index] = pc.viewInverse * p_cam;
}