#version 450
// TSDF Fusion compute shader.
//
// Per-voxel approach: each thread processes one voxel in the 3D volume.
// For each voxel:
//   1. Compute its world-space position
//   2. Project onto the depth image using camera intrinsics and view matrix
//   3. Look up the measured depth at the projected pixel
//   4. Compute the signed distance: depth_measured - voxel_depth
//   5. If within truncation band, fuse into the running TSDF average
//
// Volume format: R32_UINT, packed as:
//   bits [31:16] = TSDF distance (biased int16: [0,65535] maps to [-1,1])
//   bits [15:0]  = weight (uint16)
//
// Sign convention: positive = free space, zero = surface, negative = inside object.

// binding 0: TSDF volume (R32_UINT, read/write)
layout(set = 0, binding = 0, r32ui) uniform uimage3D tsdfVolume;

// binding 1: depth buffer (uint16 packed as uint32, from ARCore)
layout(set = 0, binding = 1) buffer DepthBuffer {
    uint data[];
} depthBuffer;

// binding 2: camera intrinsics (fx, fy, cx, cy)
layout(set = 0, binding = 2) buffer IntrinsicsBuffer {
    float fx;
    float fy;
    float cx;
    float cy;
} intrinsics;

layout(push_constant) uniform PushConstants {
    mat4 viewMatrix;       // world → camera transform
    float truncationDist;  // truncation distance in meters (e.g. 0.06)
    float scale;           // meters → voxel units (e.g. 200.0 for 0.5cm voxels)
    uint volumeSize;       // side length (e.g. 256)
    uint depthWidth;       // depth image width in pixels
    uint depthHeight;      // depth image height in pixels
    float maxWeight;       // weight cap (e.g. 200.0)
} pc;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 4) in;

// ── Packing helpers ─────────────────────────────────────────────────────

// Pack TSDF distance [-1,1] and weight [0,65535] into a uint32
uint packTsdf(float tsdf, float weight) {
    // Map [-1, 1] → [0, 65535] with midpoint at 32768 = surface
    float biased = clamp(tsdf, -1.0, 1.0) * 32767.0 + 32768.0;
    uint tsdfBits = uint(clamp(biased, 0.0, 65535.0));
    uint weightBits = uint(clamp(weight, 0.0, 65535.0));
    return (tsdfBits << 16u) | weightBits;
}

// Unpack uint32 → TSDF distance and weight
void unpackTsdf(uint packed, out float tsdf, out float weight) {
    uint tsdfBits = (packed >> 16u) & 0xFFFFu;
    tsdf = (float(tsdfBits) - 32768.0) / 32767.0;
    weight = float(packed & 0xFFFFu);
}

// ── Depth readback ──────────────────────────────────────────────────────

// Read a uint16 depth value from the packed depth buffer (same as deprojection shader)
float readDepth(uint u, uint v) {
    uint index = v * pc.depthWidth + u;
    uint packed = depthBuffer.data[index >> 1u];
    uint raw16 = (index & 1u) == 0u ? (packed & 0xFFFFu) : (packed >> 16u);
    return float(raw16) / 1000.0; // millimeters → meters
}

void main() {
    uvec3 voxelCoord = gl_GlobalInvocationID;

    // Bounds check
    if (any(greaterThanEqual(voxelCoord, uvec3(pc.volumeSize))))
        return;

    // ── Step 1: Voxel center → world position ───────────────────────────
    // Volume center = world origin. scale converts meters → voxel units.
    float invScale = 1.0 / pc.scale;
    int halfSize = int(pc.volumeSize) / 2;
    vec3 worldPos = (vec3(voxelCoord) - vec3(float(halfSize))) * invScale;

    // ── Step 2: World → camera space ────────────────────────────────────
    vec4 camPos4 = pc.viewMatrix * vec4(worldPos, 1.0);
    vec3 camPos = camPos4.xyz;

    // In ARCore camera space, -Z is forward. Voxel must be in front of camera.
    if (camPos.z >= 0.0) return;

    float voxelDepth = -camPos.z; // positive distance along camera axis

    // Early exit: skip voxels too far from the camera (depth sensor range ~5m)
    if (voxelDepth > 3.0) return;

    // ── Step 3: Project to pixel coordinates ────────────────────────────
    // The deprojection shader uses: y_cam = (v - cy) / fy * depth, z_cam = -depth
    // So the inverse is: v = fy * y_cam / depth + cy = fy * y_cam / (-z_cam) + cy
    // Both y_cam and v increase in the same direction (image convention).
    float invZ = 1.0 / (-camPos.z);
    float u_f = intrinsics.fx * camPos.x * invZ + intrinsics.cx;
    float v_f = intrinsics.fy * camPos.y * invZ + intrinsics.cy;

    // Bounds check (with 1px margin to avoid edge artifacts)
    if (u_f < 1.0 || u_f >= float(pc.depthWidth) - 1.0 ||
        v_f < 1.0 || v_f >= float(pc.depthHeight) - 1.0)
        return;

    uint u = uint(u_f);
    uint v = uint(v_f);

    // ── Step 4: Read measured depth ─────────────────────────────────────
    float depthMeasured = readDepth(u, v);

    // Invalid depth (sensor returned 0)
    if (depthMeasured <= 0.0) return;

    // ── Step 5: Compute signed distance ─────────────────────────────────
    // SDF > 0 → voxel is closer to camera than the surface (free space)
    // SDF < 0 → voxel is farther from camera than the surface (inside object)
    float sdf = depthMeasured - voxelDepth;

    // ── Step 6: Truncation and fusion ───────────────────────────────────
    float truncDist = pc.truncationDist;

    // Skip voxels outside the truncation band entirely.
    // No free-space carving — this is a static-scene accumulator.
    // Voxels in front of the surface (sdf > truncDist) are left unchanged.
    // Voxels behind the surface (sdf < -truncDist) are also left unchanged.
    if (abs(sdf) > truncDist) return;

    // Within truncation band: fuse the normalized SDF
    float tsdfNew = clamp(sdf / truncDist, -1.0, 1.0);

    // Read current TSDF value
    uint packedCurrent = imageLoad(tsdfVolume, ivec3(voxelCoord)).r;
    float tsdfOld, weightOld;
    unpackTsdf(packedCurrent, tsdfOld, weightOld);

    // Weighted running average
    float wNew = 1.0;
    float tsdfFused = (tsdfOld * weightOld + tsdfNew * wNew) / (weightOld + wNew);
    float weightFused = min(weightOld + wNew, pc.maxWeight);

    imageStore(tsdfVolume, ivec3(voxelCoord), uvec4(packTsdf(tsdfFused, weightFused), 0, 0, 0));
}
