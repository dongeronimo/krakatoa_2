#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
//Takes the ar depth buffer (from ArFrame_acquireDepthImage16Bits) and deproject its depth values,
//getting their values in world coordinates.
// Intrinsics da câmera (ARCore)
//float fx, fy;   // focal length em pixels
//float cx, cy;   // principal point
//
//// Para cada pixel (u, v) do depth buffer:
//float depth = depthBuffer[v * width + u];  // metros
//
//if (depth <= 0.0f) continue;  // pixel inválido
//
//// Passo 1: pixel → espaço da câmera
//float x_cam = (u - cx) / fx * depth;
//float y_cam = (v - cy) / fy * depth;
//float z_cam = -depth;  // convenção câmera aponta -Z
//
//// Passo 2: câmera → mundo
//glm::vec4 p_cam = {x_cam, y_cam, z_cam, 1.0f};
//glm::mat4 view_inv = glm::inverse(viewMatrix);
//glm::vec4 p_world  = view_inv * p_cam;
// Buffer 0: camera intrinsics + image dimensions
layout (set = 0, binding = 0) buffer UBO {
    float fx; //Focal length in px
    float fy; //Focal length in px
    float cx; //Principal point
    float cy; //Principal point
} ubo;

// Buffer 1: the depth data
layout(set = 0, binding = 1) buffer DepthBuffer {
    uint16_t data[];
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

    // Read and convert depth (ARCore uint16 is in millimeters)
    float depth = float(uint(depthBuffer.data[index])) / 1000.0;

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