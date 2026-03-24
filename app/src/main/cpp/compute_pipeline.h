#ifndef KRAKATOA_COMPUTE_PIPELINE_H
#define KRAKATOA_COMPUTE_PIPELINE_H
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include "ring_buffer.h"
#include "vk_mem_alloc.h"
#define MAX_COMPUTE_DESCRIPTOR_SETS 4096
namespace graphics {
    /**
     * Minimal config for creating a ComputePipeline.
     * Just the shader name and descriptor pool sizing — all dispatch logic
     * lives in the ComputeOperation subclasses, not in callbacks.
     */
    struct ComputePipelineConfig {
        // e.g. "depth_deproject" → loads "shaders/depth_deproject.comp.spv"
        std::string shaderName;
        // Descriptor pool sizes — declare what bindings you need
        // e.g. { {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4} }
        std::vector<VkDescriptorPoolSize> descriptorPoolSizes;
    };

    /**
     * Thin Vulkan wrapper around a compute pipeline object.
     *
     * Owns the VkPipeline, descriptor pool, and pre-allocated ring-buffered
     * descriptor sets. Does NOT own the pipeline layout or descriptor set
     * layout — those belong to the ComputeOperation that created this.
     *
     * ComputeOperation subclasses call Bind(), GetDescriptorSet(), and
     * DispatchRaw() directly.
     */
    class ComputePipeline {
    public:
        ComputePipeline(VkDevice device,
                        VmaAllocator allocator,
                        const ComputePipelineConfig& config,
                        VkPipelineLayout pipelineLayout,
                        VkDescriptorSetLayout descriptorSetLayout);
        ~ComputePipeline();

        // Non-copyable
        ComputePipeline(const ComputePipeline&) = delete;
        ComputePipeline& operator=(const ComputePipeline&) = delete;

        /** Bind this compute pipeline to the command buffer. */
        void Bind(VkCommandBuffer cmd) const;

        /** Get the pre-allocated descriptor set for a given frame index. */
        VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const;

        /** Issue a vkCmdDispatch with the given workgroup counts. */
        void DispatchRaw(VkCommandBuffer cmd,
                         uint32_t x, uint32_t y, uint32_t z) const;

        /** Allocate an additional descriptor set from the pool (for special use). */
        VkDescriptorSet AllocateDescriptorSet();

        VkPipeline GetPipeline() const { return pipeline; }
        VkDevice GetDevice() const { return device; }
        VmaAllocator GetAllocator() const { return allocator; }
        VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }

    private:
        VkDevice device = VK_NULL_HANDLE;               // not owned
        VmaAllocator allocator = VK_NULL_HANDLE;         // not owned
        VkPipeline pipeline = VK_NULL_HANDLE;            // owned
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;   // not owned (ComputeOperation owns it)
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE; // not owned
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;   // owned
        utils::RingBuffer<VkDescriptorSet> descriptorSets;  // pre-allocated per frame

        VkShaderModule CreateShaderModule(const std::vector<uint8_t>& data);
    };
}

#endif //KRAKATOA_COMPUTE_PIPELINE_H
