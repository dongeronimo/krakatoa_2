#ifndef KRAKATOA_VOXELIZATION_OP_H
#define KRAKATOA_VOXELIZATION_OP_H

#include "compute_operation.h"

namespace graphics {

    /**
     * Compute operation: voxelization.
     *
     * Takes world-space positions (from DepthDeprojectionOp) and accumulates
     * them into a 3D occupancy volume (R8_UINT storage image).
     *
     * Does not own the volume image — it's a shared VoxelVolume resource.
     * Does not own the position source — it reads from an upstream operation.
     *
     * Shader: voxelization.comp
     *   binding 0: positions SSBO (vec4[], from deprojection)
     *   binding 1: 3D volume storage image (r8ui)
     *   push constants: positionCount, scale, volumeSize
     */
    class VoxelizationOp : public ComputeOperation {
    public:
        explicit VoxelizationOp(const InitContext& ctx);
        ~VoxelizationOp() override = default;

        // ── Lifecycle ───────────────────────────────────────────────────
        void Initialize() override;
        void Execute(VkCommandBuffer cmd, uint32_t frameIndex) override;

        // ── One-time wiring (call before Initialize) ────────────────────

        /**
         * Link to the upstream operation that produces the position buffer.
         * Uses the generic ComputeOperation interface so any operation that
         * outputs a buffer can serve as the source.
         */
        void SetPositionSource(ComputeOperation* source);

        /** The 3D volume image view to accumulate voxels into. */
        void SetVolumeImageView(VkImageView view);

        // ── Per-frame setters ───────────────────────────────────────────
        void SetScale(float scale);
        void SetVolumeSize(uint32_t size);
        void SetPositionCount(uint32_t count);

    private:
        // ── Push constant layout (must match shader) ────────────────────
        struct PushConstant {
            uint32_t positionCount;
            float    scale;       // meters → voxel units (100.0 = 1cm voxels)
            uint32_t volumeSize;  // side length of the 3D volume (e.g. 256)
        };

        // ── Wiring ─────────────────────────────────────────────────────
        ComputeOperation* positionSource = nullptr;  // non-owning
        VkImageView volumeView = VK_NULL_HANDLE;

        // ── Per-frame params ────────────────────────────────────────────
        float scale_ = 100.0f;
        uint32_t volumeSize_ = 256;
        uint32_t positionCount_ = 0;
    };

} // namespace graphics

#endif //KRAKATOA_VOXELIZATION_OP_H
