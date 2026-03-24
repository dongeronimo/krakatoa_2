#ifndef KRAKATOA_TSDF_FUSION_OP_H
#define KRAKATOA_TSDF_FUSION_OP_H

#include "compute_operation.h"
#include "ring_buffer.h"
#include <array>

namespace graphics {

    /**
     * Compute operation: TSDF fusion.
     *
     * Per-voxel TSDF integration. Each thread processes one voxel:
     *   1. Projects the voxel to the depth image
     *   2. Computes signed distance from the surface
     *   3. Fuses into the running weighted average stored in the volume
     *
     * Static-scene accumulator — no free-space carving. Geometry persists
     * across frames with high weight cap for stable reconstruction.
     *
     * Shader: tsdf_fusion.comp
     *   binding 0: TSDF volume (r32ui storage image, packed distance+weight)
     *   binding 1: depth buffer SSBO (uint16 packed)
     *   binding 2: camera intrinsics SSBO (fx, fy, cx, cy)
     *   push constants: viewMatrix (mat4), truncationDist, scale, volumeSize,
     *                   depthWidth, depthHeight, maxWeight
     */
    class TsdfFusionOp : public ComputeOperation {
    public:
        explicit TsdfFusionOp(const InitContext& ctx);
        ~TsdfFusionOp() override;

        // ── Lifecycle ───────────────────────────────────────────────────
        void Initialize() override;
        void Execute(VkCommandBuffer cmd, uint32_t frameIndex) override;

        // ── One-time wiring (call before Initialize) ────────────────────

        /** The TSDF volume image view to fuse into. */
        void SetVolumeImageView(VkImageView view);

        // ── Per-frame setters ───────────────────────────────────────────

        /** The depth SSBO uploaded by ArDepthImage for this frame. */
        void SetDepthBuffer(VkBuffer depthSSBO);

        /** Scaled camera intrinsics (in depth-image pixel units). */
        void SetIntrinsics(float fx, float fy, float cx, float cy);

        /** Camera view matrix (world → camera). NOT the inverse. */
        void SetViewMatrix(const std::array<float,16>& viewMat);

        /** Depth image dimensions. */
        void SetDepthDimensions(uint32_t w, uint32_t h);

        /** Meters → voxel units. 200.0 = 0.5cm voxels. */
        void SetScale(float scale);

        /** Volume side length. */
        void SetVolumeSize(uint32_t size);

        /** Truncation distance in meters (e.g. 0.06 for 6cm). */
        void SetTruncationDistance(float dist);

        /** Maximum weight cap (e.g. 200). Higher = more persistent geometry. */
        void SetMaxWeight(float maxWeight);

    private:
        // ── Push constant layout (must match shader) ────────────────────
        struct PushConstant {
            float viewMatrix[16]; // mat4 — world → camera
            float truncationDist;
            float scale;
            uint32_t volumeSize;
            uint32_t depthWidth;
            uint32_t depthHeight;
            float maxWeight;
        };

        // ── Intrinsics UBO layout (must match shader binding 2) ─────────
        struct IntrinsicsUbo {
            float fx, fy, cx, cy;
        };

        // ── Wiring ─────────────────────────────────────────────────────
        VkImageView volumeView = VK_NULL_HANDLE;

        // ── Per-frame input state ───────────────────────────────────────
        VkBuffer currentDepthBuffer = VK_NULL_HANDLE;
        float fx_ = 0, fy_ = 0, cx_ = 0, cy_ = 0;
        std::array<float,16> viewMatrix_{};
        uint32_t depthWidth_ = 0, depthHeight_ = 0;
        float scale_ = 200.0f;
        uint32_t volumeSize_ = 256;
        float truncationDist_ = 0.06f;
        float maxWeight_ = 200.0f;

        // ── Owned GPU resources ─────────────────────────────────────────
        // Intrinsics: ring-buffered host-visible SSBOs (one per frame in flight)
        utils::RingBuffer<VkBuffer>      intrinsicsBuffers;
        utils::RingBuffer<VmaAllocation> intrinsicsAllocations;
        utils::RingBuffer<void*>         intrinsicsMappedPtrs;

        void CreateIntrinsicsBuffers();
    };

} // namespace graphics

#endif //KRAKATOA_TSDF_FUSION_OP_H
