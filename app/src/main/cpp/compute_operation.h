#ifndef KRAKATOA_COMPUTE_OPERATION_H
#define KRAKATOA_COMPUTE_OPERATION_H

#include <vulkan/vulkan.h>
#include <string>
#include <memory>
#include <vector>
#include "vk_mem_alloc.h"

namespace graphics {

    class ComputePipeline;

    /**
     * Abstract base class for GPU compute operations.
     *
     * Each subclass encapsulates a single compute shader stage: it owns
     * the descriptor set layout, pipeline layout, ComputePipeline, and
     * any persistent GPU resources (lookup tables, output ring buffers, etc.).
     *
     * Lifecycle:
     *   1. Construct with an InitContext (device + allocator).
     *   2. Feed any runtime parameters via typed setters.
     *   3. Call Initialize() once all required params are available.
     *   4. Each frame: update per-frame inputs via setters, then Execute().
     *   5. Call InsertPostBarrier() between stages as needed.
     *
     * Subclasses override Initialize() and Execute(). They may also override
     * InsertPostBarrier() if the default compute-write → compute-read barrier
     * doesn't match their output usage.
     *
     * Output convention: subclasses that produce buffers or images should
     * override GetOutputBuffer() / GetOutputImageView() so that downstream
     * operations can wire themselves generically via ComputeOperation* rather
     * than needing the concrete type.
     */
    class ComputeOperation {
    public:
        /// Everything a compute operation needs from the engine at construction time.
        struct InitContext {
            VkDevice device;
            VmaAllocator allocator;
        };

        ComputeOperation(const InitContext& ctx, const std::string& name);
        virtual ~ComputeOperation();

        // Non-copyable
        ComputeOperation(const ComputeOperation&) = delete;
        ComputeOperation& operator=(const ComputeOperation&) = delete;

        // ── Lifecycle ───────────────────────────────────────────────────

        /**
         * One-time setup. Subclass creates its descriptor set layout,
         * pipeline layout, ComputePipeline, and any persistent GPU resources.
         * Must be called after all required setters have been called.
         */
        virtual void Initialize() = 0;

        /**
         * Per-frame work. Updates descriptors, pushes constants, dispatches.
         * @param cmd        The command buffer being recorded for this frame.
         * @param frameIndex Ring-buffer index (0 .. MAX_FRAMES_IN_FLIGHT-1).
         */
        virtual void Execute(VkCommandBuffer cmd, uint32_t frameIndex) = 0;

        /**
         * Insert a pipeline barrier after this operation's compute writes.
         *
         * The default implementation inserts a full VkMemoryBarrier:
         *   src = COMPUTE_SHADER_BIT / SHADER_WRITE
         *   dst = COMPUTE_SHADER_BIT / SHADER_READ
         *
         * Override this in subclasses whose output feeds non-compute stages
         * (e.g. vertex input, transfer, fragment).
         */
        virtual void InsertPostBarrier(VkCommandBuffer cmd);

        // ── Output interface ────────────────────────────────────────────
        // Subclasses override these to expose their outputs generically.
        // Downstream operations can link via ComputeOperation* and call these.

        /**
         * Return the output buffer for the given frame, or VK_NULL_HANDLE
         * if this operation doesn't produce a buffer.
         */
        virtual VkBuffer GetOutputBuffer(uint32_t frameIndex) const;

        /**
         * Return the output image view, or VK_NULL_HANDLE if this
         * operation doesn't produce an image.
         */
        virtual VkImageView GetOutputImageView() const;

        // ── Accessors ───────────────────────────────────────────────────
        const std::string& GetName() const { return name; }
        bool IsInitialized() const { return initialized; }

    protected:
        VkDevice device;
        VmaAllocator allocator;
        std::string name;
        bool initialized = false;

        // Vulkan objects owned by this operation.
        // Subclasses populate these in Initialize().
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<ComputePipeline> pipeline;

        // ── Helpers for subclasses ──────────────────────────────────────

        /** Create a descriptor set layout from a list of bindings. */
        VkDescriptorSetLayout CreateDescriptorSetLayout(
            const std::vector<VkDescriptorSetLayoutBinding>& bindings);

        /**
         * Create a pipeline layout.
         * @param dsLayout       The descriptor set layout for set 0.
         * @param pushConstant   Optional push constant range (nullptr = none).
         */
        VkPipelineLayout CreatePipelineLayout(
            VkDescriptorSetLayout dsLayout,
            const VkPushConstantRange* pushConstant = nullptr);
    };

} // namespace graphics

#endif //KRAKATOA_COMPUTE_OPERATION_H
