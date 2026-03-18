#ifndef KRAKATOA_MARCHING_CUBES_OP_H
#define KRAKATOA_MARCHING_CUBES_OP_H

#include "compute_operation.h"

namespace graphics {

    class GpuMesh;

    /**
     * Compute operation: marching cubes mesh generation from TSDF volume.
     *
     * Reads a 3D TSDF volume (R32_UINT, packed distance+weight) and generates
     * triangle mesh geometry into a GpuMesh (vertex + index + counter buffers).
     * Surface extraction follows zero-crossings of the signed distance field.
     *
     * Owns the marching cubes lookup table buffers (edge table, tri table).
     * Does not own the volume image or the GpuMesh — those are shared resources.
     *
     * Shader: marching_cubes.comp
     *   binding 0: 3D TSDF volume storage image (r32ui)
     *   binding 1: edge table SSBO (256 ints)
     *   binding 2: tri table SSBO (256*16 ints)
     *   binding 3: vertex output SSBO
     *   binding 4: index output SSBO
     *   binding 5: atomic counter SSBO
     *   push constants: scale, maxDistance, volumeSize, maxVertices, maxIndices, minWeight
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
        void SetScale(float scale);
        void SetMaxDistance(float maxDist);
        void SetMinWeight(float minWeight);

    private:
        // ── Push constant layout (must match shader) ────────────────────
        struct PushConstant {
            float    scale;       // same scale as TSDF fusion (e.g. 200.0 = 0.5cm)
            float    maxDistance;  // max edge length in voxel units
            uint32_t volumeSize;
            uint32_t maxVertices;
            uint32_t maxIndices;
            float    minWeight;   // minimum TSDF weight to consider a voxel valid
        };

        // ── Wiring ─────────────────────────────────────────────────────
        VkImageView volumeView = VK_NULL_HANDLE;
        GpuMesh* outputMesh = nullptr;  // non-owning

        // ── Per-frame params ────────────────────────────────────────────
        float scale_ = 200.0f;
        float maxDistance_ = 2.0f;
        float minWeight_ = 2.0f;

        // ── Owned lookup table buffers ──────────────────────────────────
        VkBuffer      edgeTableBuffer     = VK_NULL_HANDLE;
        VmaAllocation edgeTableAllocation  = VK_NULL_HANDLE;
        VkBuffer      triTableBuffer      = VK_NULL_HANDLE;
        VmaAllocation triTableAllocation   = VK_NULL_HANDLE;

        void CreateLookupTables();
    };

} // namespace graphics

#endif //KRAKATOA_MARCHING_CUBES_OP_H
