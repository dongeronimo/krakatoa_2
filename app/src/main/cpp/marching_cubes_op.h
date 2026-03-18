#ifndef KRAKATOA_MARCHING_CUBES_OP_H
#define KRAKATOA_MARCHING_CUBES_OP_H

#include "compute_operation.h"

namespace graphics {

    class GpuMesh;

    /**
     * Compute operation: marching cubes mesh generation.
     *
     * Reads a 3D voxel volume and generates triangle mesh geometry into
     * a GpuMesh (vertex + index + counter buffers).
     *
     * Owns the marching cubes lookup table buffers (edge table, tri table).
     * Does not own the volume image or the GpuMesh — those are shared resources.
     *
     * Shader: marching_cubes.comp
     *   binding 0: 3D volume storage image (r8ui)
     *   binding 1: edge table SSBO (256 ints)
     *   binding 2: tri table SSBO (256*16 ints)
     *   binding 3: vertex output SSBO
     *   binding 4: index output SSBO
     *   binding 5: atomic counter SSBO
     *   push constants: cutoff, scale, maxDistance, volumeSize, maxVertices, maxIndices
     */
    class MarchingCubesOp : public ComputeOperation {
    public:
        explicit MarchingCubesOp(const InitContext& ctx);
        ~MarchingCubesOp() override;

        // ── Lifecycle ───────────────────────────────────────────────────
        void Initialize() override;
        void Execute(VkCommandBuffer cmd, uint32_t frameIndex) override;

        // Barrier: compute write → vertex input + transfer
        // (mesh output feeds the rendering pipeline and indirect draw copy)
        void InsertPostBarrier(VkCommandBuffer cmd) override;

        // ── One-time wiring (call before Initialize) ────────────────────

        /** The 3D volume image view to read voxels from. */
        void SetVolumeImageView(VkImageView view);

        /** The GPU mesh that receives generated vertices/indices. Non-owning. */
        void SetOutputMesh(GpuMesh* mesh);

        // ── Per-frame setters ───────────────────────────────────────────
        void SetCutoff(uint32_t cutoff);
        void SetScale(float scale);
        void SetMaxDistance(float maxDist);

    private:
        // ── Push constant layout (must match shader) ────────────────────
        struct PushConstant {
            uint32_t cutoff;
            float    scale;       // same scale as voxelization (100.0 = 1cm)
            float    maxDistance;  // max edge length in voxel units
            uint32_t volumeSize;
            uint32_t maxVertices;
            uint32_t maxIndices;
        };

        // ── Wiring ─────────────────────────────────────────────────────
        VkImageView volumeView = VK_NULL_HANDLE;
        GpuMesh* outputMesh = nullptr;  // non-owning

        // ── Per-frame params ────────────────────────────────────────────
        uint32_t cutoff_ = 127;
        float scale_ = 100.0f;
        float maxDistance_ = 2.0f;

        // ── Owned lookup table buffers ──────────────────────────────────
        VkBuffer      edgeTableBuffer     = VK_NULL_HANDLE;
        VmaAllocation edgeTableAllocation  = VK_NULL_HANDLE;
        VkBuffer      triTableBuffer      = VK_NULL_HANDLE;
        VmaAllocation triTableAllocation   = VK_NULL_HANDLE;

        void CreateLookupTables();
    };

} // namespace graphics

#endif //KRAKATOA_MARCHING_CUBES_OP_H
