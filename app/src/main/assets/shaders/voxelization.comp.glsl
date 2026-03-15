#version 450
// Takes world-space positions from the deprojection step and accumulates them
// into a 1024³ uint8 3D volume. Each voxel is 1 cm³ and the center of the
// volume (512, 512, 512) corresponds to the world origin.
// Values are incremented on each hit (clamped at 255) so that frequently
// observed voxels score higher — useful for noise filtering before marching cubes.

// Input: world-space positions from deprojection (vec4, w unused)
layout(set = 0, binding = 0) buffer PositionBuffer {
    vec4 positions[];
} inPositions;

// Output: 3D occupancy volume (1024³, R8_UINT)
layout(set = 0, binding = 1, r8ui) uniform uimage3D volumeTexture;

layout(push_constant) uniform PushConstants {
    uint positionCount; // total number of positions to process
} pc;

layout(local_size_x = 256) in;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= pc.positionCount) return;

    vec4 pos = inPositions.positions[idx];
    // Invalid pixels were zeroed out by the deprojection shader
    if (pos.x == 0.0 && pos.y == 0.0 && pos.z == 0.0) return;

    // World coordinates are in meters. Convert to centimeters for the voxel grid.
    vec3 posCm = pos.xyz * 100.0;

    // Volume centre (512, 512, 512) = world origin.
    ivec3 voxelCoord = ivec3(floor(posCm)) + ivec3(512);

    // Bounds check — discard points outside the 1024³ cube
    if (any(lessThan(voxelCoord, ivec3(0))) || any(greaterThanEqual(voxelCoord, ivec3(1024))))
        return;

    // Read-modify-write: increment the occupancy counter, clamp at 255.
    // Not atomic — acceptable for a probabilistic occupancy accumulator.
    uint current = imageLoad(volumeTexture, voxelCoord).r;
    if (current < 255u) {
        imageStore(volumeTexture, voxelCoord, uvec4(current + 1u, 0, 0, 0));
    }
}
