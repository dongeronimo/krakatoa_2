#ifndef KRAKATOA_DEPTH_DEPROJECTION_OP_H
#define KRAKATOA_DEPTH_DEPROJECTION_OP_H

#include "compute_operation.h"
#include "ring_buffer.h"
#include <array>

namespace graphics {

    /**
     * Compute operation: depth deprojection.
     *
     * Takes an ARCore depth buffer (uint16 SSBO) and camera intrinsics,
     * and produces a buffer of world-space vec4 positions (one per pixel).
     *
     * Resources owned:
     *   - Output position ring buffers  (one per frame in flight)
     *   - Intrinsics SSBO ring buffers  (one per frame, host-visible)
     *
     * Shader: deprojection.comp
     *   binding 0: intrinsics SSBO (fx, fy, cx, cy)
     *   binding 1: depth input SSBO (uint16[])
     *   binding 2: output SSBO (vec4[])
     *   push constants: viewInverse (mat4), width, height
     */
    class DepthDeprojectionOp : public ComputeOperation {
    public:
        explicit DepthDeprojectionOp(const InitContext& ctx);
        ~DepthDeprojectionOp() override;

        // ── Lifecycle ───────────────────────────────────────────────────
        void Initialize() override;
        void Execute(VkCommandBuffer cmd, uint32_t frameIndex) override;

        // Barrier: compute write → compute read + vertex read
        // (deprojection output feeds both voxelization and potentially vertex shaders)
        void InsertPostBarrier(VkCommandBuffer cmd) override;

        // ── Per-frame setters (call before Execute) ─────────────────────

        /** The depth SSBO uploaded by ArDepthImage for this frame. */
        void SetDepthBuffer(VkBuffer depthSSBO);

        /** Scaled camera intrinsics (in depth-image pixel units). */
        void SetIntrinsics(float fx, float fy, float cx, float cy);

        /** Camera view-inverse matrix (camera → world). */
        void SetViewInverse(const std::array<float,16>& viewInv);

        /** Depth image dimensions. Must be set before Initialize(). */
        void SetDimensions(uint32_t w, uint32_t h);

        // ── Output interface ────────────────────────────────────────────

        /** World-space positions buffer for the given frame. */
        VkBuffer GetOutputBuffer(uint32_t frameIndex) const override;

        /** Number of positions produced (width * height). */
        uint32_t GetPositionCount() const { return width * height; }

    private:
        // ── Push constant layout (must match shader) ────────────────────
        struct PushConstant {
            float viewInverse[16]; // mat4
            uint32_t width;
            uint32_t height;
        };

        // ── Intrinsics UBO layout (must match shader binding 0) ─────────
        struct IntrinsicsUbo {
            float fx, fy, cx, cy;
        };

        // ── Per-frame input state ───────────────────────────────────────
        VkBuffer currentDepthBuffer = VK_NULL_HANDLE;
        float fx_ = 0, fy_ = 0, cx_ = 0, cy_ = 0;
        std::array<float,16> viewInverse_{};
        uint32_t width = 0, height = 0;

        // ── Owned GPU resources ─────────────────────────────────────────
        // Output: ring-buffered vec4 position buffers (one per frame in flight)
        utils::RingBuffer<VkBuffer>        outputBuffers;
        utils::RingBuffer<VmaAllocation>   outputAllocations;

        // Intrinsics: ring-buffered host-visible SSBOs (one per frame in flight)
        utils::RingBuffer<VkBuffer>        intrinsicsBuffers;
        utils::RingBuffer<VmaAllocation>   intrinsicsAllocations;
        utils::RingBuffer<void*>           intrinsicsMappedPtrs;

        // ── Internal helpers ────────────────────────────────────────────
        void CreateOutputBuffers();
        void CreateIntrinsicsBuffers();
    };

} // namespace graphics

#endif //KRAKATOA_DEPTH_DEPROJECTION_OP_H
