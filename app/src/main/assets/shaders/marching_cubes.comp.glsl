#version 450
// Marching cubes compute shader.
// Reads a 1024³ R8_UINT occupancy volume and generates triangle mesh output.
// Each thread processes one voxel cell (i,j,k) → (i+1,j+1,k+1).
//
// Vertex format: interleaved [px, py, pz, nx, ny, nz, u, v] (8 floats = 32 bytes)
// to match the existing rendering pipeline.

// binding 0: 3D volume texture (input, R8_UINT)
layout(set = 0, binding = 0, r8ui) uniform readonly uimage3D volumeTexture;

// binding 1: edge table (256 ints)
layout(set = 0, binding = 1) buffer EdgeTable {
    int data[];
} edgeTable;

// binding 2: triangle table (256 * 16 ints)
layout(set = 0, binding = 2) buffer TriTable {
    int data[];
} triTable;

// binding 3: output vertices (interleaved float: pos3 + normal3 + uv2)
layout(set = 0, binding = 3) buffer VertexBuffer {
    float data[];
} outVertices;

// binding 4: output indices (uint32)
layout(set = 0, binding = 4) buffer IndexBuffer {
    uint data[];
} outIndices;

// binding 5: atomic counters [vertexCount, indexCount]
layout(set = 0, binding = 5) buffer AtomicCounters {
    uint vertexCount;
    uint indexCount;
} counters;

layout(push_constant) uniform PushConstants {
    uint cutoff;       // occupancy threshold (e.g. 127)
    float scale;       // voxel-to-world scale (inverse of voxelization scale)
    float maxDistance;  // max edge length before discontinuity (in voxel units, e.g. 2.0)
    uint volumeSizeX;  // X dimension
    uint volumeSizeY;  // Y dimension
    uint volumeSizeZ;  // Z dimension
    uint maxVertices;  // output buffer capacity (vertices)
    uint maxIndices;   // output buffer capacity (indices)
} pc;

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

// Sample the volume at integer coordinates. Returns 0 if out of bounds.
float sampleVolume(ivec3 p) {
    ivec3 volSize = ivec3(int(pc.volumeSizeX), int(pc.volumeSizeY), int(pc.volumeSizeZ));
    if (any(lessThan(p, ivec3(0))) || any(greaterThanEqual(p, volSize)))
        return 0.0;
    return float(imageLoad(volumeTexture, p).r);
}

// Compute gradient (central differences) for normal estimation
vec3 computeGradient(ivec3 p) {
    float dx = sampleVolume(p + ivec3(1,0,0)) - sampleVolume(p - ivec3(1,0,0));
    float dy = sampleVolume(p + ivec3(0,1,0)) - sampleVolume(p - ivec3(0,1,0));
    float dz = sampleVolume(p + ivec3(0,0,1)) - sampleVolume(p - ivec3(0,0,1));
    return vec3(dx, dy, dz);
}

// Interpolate vertex position along an edge between two corners
vec3 interpolateEdge(vec3 p0, vec3 p1, float v0, float v1) {
    float threshold = float(pc.cutoff);
    if (abs(v0 - v1) < 0.001) return (p0 + p1) * 0.5;
    float t = (threshold - v0) / (v1 - v0);
    t = clamp(t, 0.0, 1.0);
    return mix(p0, p1, t);
}

// The 8 corner offsets of a cube cell
const ivec3 cornerOffsets[8] = ivec3[8](
    ivec3(0, 0, 0), // 0
    ivec3(1, 0, 0), // 1
    ivec3(1, 1, 0), // 2
    ivec3(0, 1, 0), // 3
    ivec3(0, 0, 1), // 4
    ivec3(1, 0, 1), // 5
    ivec3(1, 1, 1), // 6
    ivec3(0, 1, 1)  // 7
);

// Edge endpoint pairs (vertex indices for each of the 12 edges)
const ivec2 edgeEndpoints[12] = ivec2[12](
    ivec2(0, 1), ivec2(1, 2), ivec2(2, 3), ivec2(3, 0),
    ivec2(4, 5), ivec2(5, 6), ivec2(6, 7), ivec2(7, 4),
    ivec2(0, 4), ivec2(1, 5), ivec2(2, 6), ivec2(3, 7)
);

void main() {
    uvec3 cell = gl_GlobalInvocationID;

    // Each cell spans (cell) to (cell+1), so the last valid cell is volumeSize-2
    if (cell.x >= pc.volumeSizeX - 1 || cell.y >= pc.volumeSizeY - 1 || cell.z >= pc.volumeSizeZ - 1) return;

    ivec3 basePos = ivec3(cell);

    // Sample the 8 corners of this cell
    float cornerValues[8];
    for (int i = 0; i < 8; i++) {
        cornerValues[i] = sampleVolume(basePos + cornerOffsets[i]);
    }

    // Build the case index: bit i is set if corner i is above threshold
    float threshold = float(pc.cutoff);
    int cubeIndex = 0;
    for (int i = 0; i < 8; i++) {
        if (cornerValues[i] >= threshold) {
            cubeIndex |= (1 << i);
        }
    }

    // No triangles for this cell
    int edges = edgeTable.data[cubeIndex];
    if (edges == 0) return;

    // Compute the 12 possible edge intersection points
    vec3 edgeVertices[12];
    vec3 edgeNormals[12];
    for (int i = 0; i < 12; i++) {
        if ((edges & (1 << i)) != 0) {
            int a = edgeEndpoints[i].x;
            int b = edgeEndpoints[i].y;
            vec3 pa = vec3(basePos + cornerOffsets[a]);
            vec3 pb = vec3(basePos + cornerOffsets[b]);
            edgeVertices[i] = interpolateEdge(pa, pb, cornerValues[a], cornerValues[b]);

            // Interpolate normals from gradients at the two corners
            vec3 na = computeGradient(basePos + cornerOffsets[a]);
            vec3 nb = computeGradient(basePos + cornerOffsets[b]);
            float t = (abs(cornerValues[a] - cornerValues[b]) < 0.001)
                ? 0.5
                : clamp((threshold - cornerValues[a]) / (cornerValues[b] - cornerValues[a]), 0.0, 1.0);
            vec3 n = mix(na, nb, t);
            float len = length(n);
            edgeNormals[i] = (len > 0.001) ? -n / len : vec3(0.0, 1.0, 0.0);
        }
    }

    // Emit triangles from the triTable
    int triTableBase = cubeIndex * 16;
    for (int i = 0; i < 15; i += 3) {
        int e0 = triTable.data[triTableBase + i];
        if (e0 == -1) break;
        int e1 = triTable.data[triTableBase + i + 1];
        int e2 = triTable.data[triTableBase + i + 2];

        vec3 v0 = edgeVertices[e0];
        vec3 v1 = edgeVertices[e1];
        vec3 v2 = edgeVertices[e2];

        // Discontinuity check: skip triangle if any edge exceeds maxDistance
        if (distance(v0, v1) > pc.maxDistance ||
            distance(v1, v2) > pc.maxDistance ||
            distance(v0, v2) > pc.maxDistance) {
            continue;
        }

        // Allocate 3 vertices and 3 indices atomically
        uint vertBase = atomicAdd(counters.vertexCount, 3);
        uint idxBase  = atomicAdd(counters.indexCount, 3);

        // Check capacity
        if (vertBase + 3 > pc.maxVertices || idxBase + 3 > pc.maxIndices) return;

        // Write vertices (8 floats each: pos3 + normal3 + uv2)
        // Convert from voxel coordinates to world coordinates:
        // world = (voxelPos - halfSize) / scale
        float invScale = 1.0 / pc.scale;
        vec3 worldCenter = vec3(float(pc.volumeSizeX), float(pc.volumeSizeY), float(pc.volumeSizeZ)) * 0.5;

        for (int vi = 0; vi < 3; vi++) {
            vec3 vpos;
            vec3 vnorm;
            if (vi == 0)      { vpos = v0; vnorm = edgeNormals[e0]; }
            else if (vi == 1) { vpos = v1; vnorm = edgeNormals[e1]; }
            else              { vpos = v2; vnorm = edgeNormals[e2]; }

            // Voxel space → world space
            vec3 worldPos = (vpos - worldCenter) * invScale;
            // UV: simple planar projection (XZ plane)
            vec2 uv = worldPos.xz;

            uint offset = (vertBase + uint(vi)) * 8;
            outVertices.data[offset + 0] = worldPos.x;
            outVertices.data[offset + 1] = worldPos.y;
            outVertices.data[offset + 2] = worldPos.z;
            outVertices.data[offset + 3] = vnorm.x;
            outVertices.data[offset + 4] = vnorm.y;
            outVertices.data[offset + 5] = vnorm.z;
            outVertices.data[offset + 6] = uv.x;
            outVertices.data[offset + 7] = uv.y;
        }

        // Write indices — reversed winding (0,2,1) so triangles are CCW in
        // Vulkan's coordinate system (Y-down in NDC vs OpenGL's Y-up).
        // The Bourke tri table assumes OpenGL/right-hand winding.
        outIndices.data[idxBase + 0] = vertBase + 0;
        outIndices.data[idxBase + 1] = vertBase + 2;
        outIndices.data[idxBase + 2] = vertBase + 1;
    }
}
