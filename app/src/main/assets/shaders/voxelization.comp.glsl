#version 450
// Takes world-space positions from the deprojection step and accumulates them
// into a volumeSizeX × volumeSizeY × volumeSizeZ uint8 3D volume. Each voxel
// is 1 cm³ and the center of the volume corresponds to the world origin.
// Values are incremented on each hit (clamped at 255) so that frequently
// observed voxels score higher — useful for noise filtering before marching cubes.

// Input: world-space positions from deprojection (vec4, w unused)
layout(set = 0, binding = 0) buffer PositionBuffer {
    vec4 positions[];
} inPositions;

// Output: 3D occupancy volume (R8_UINT, size from push constants)
layout(set = 0, binding = 1, r8ui) uniform uimage3D volumeTexture;

layout(push_constant) uniform PushConstants {
    uint positionCount; // total number of positions to process
    float scale;        // conversion factor: meters → voxel units (e.g. 100.0 for 1cm voxels)
    uint volumeSizeX;   // X dimension of the 3D volume
    uint volumeSizeY;   // Y dimension of the 3D volume
    uint volumeSizeZ;   // Z dimension of the 3D volume
} pc;

layout(local_size_x = 256) in;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= pc.positionCount) return;

    vec4 pos = inPositions.positions[idx];
    // Invalid pixels were zeroed out by the deprojection shader
    if (pos.x == 0.0 && pos.y == 0.0 && pos.z == 0.0) return;

    // World coordinates are in meters. Convert to voxel units using scale.
    // scale = 100.0 means 1 voxel = 1 cm, scale = 1000.0 means 1 voxel = 1 mm, etc.
    vec3 posScaled = pos.xyz * pc.scale;

    // Volume centre = world origin.
    ivec3 halfSize = ivec3(int(pc.volumeSizeX), int(pc.volumeSizeY), int(pc.volumeSizeZ)) / 2;
    ivec3 voxelCoord = ivec3(floor(posScaled)) + halfSize;

    // Bounds check — discard points outside the volume
    ivec3 volSize = ivec3(int(pc.volumeSizeX), int(pc.volumeSizeY), int(pc.volumeSizeZ));
    if (any(lessThan(voxelCoord, ivec3(0))) || any(greaterThanEqual(voxelCoord, volSize)))
        return;

    // Read-modify-write: increment the occupancy counter, clamp at 255.
    // Not atomic — acceptable for a probabilistic occupancy accumulator.
    uint current = imageLoad(volumeTexture, voxelCoord).r;
    if (current < 255u) {
        imageStore(volumeTexture, voxelCoord, uvec4(current + 1u, 0, 0, 0));
    }
}
