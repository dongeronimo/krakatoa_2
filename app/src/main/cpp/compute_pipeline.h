#ifndef KRAKATOA_COMPUTE_PIPELINE_H
#define KRAKATOA_COMPUTE_PIPELINE_H
#include <vulkan/vulkan.h>
#include <string>
#include <functional>
#include <vector>
#include "ring_buffer.h"
#include "vk_mem_alloc.h"
#define MAX_COMPUTE_DESCRIPTOR_SETS 4096
namespace graphics {
    class ComputePipeline;
    /**
     * Describes a compute pipeine
     * */
    struct ComputePipelineConfig {
        // e.g. "depth_deproject" → loads "shaders/depth_deproject.comp.spv"
        std::string shaderName;
        // Descriptor pool sizes — declare what bindings you need
        // e.g. { {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4} }
        std::vector<VkDescriptorPoolSize> descriptorPoolSizes;
        // Called every dispatch — bind descriptors, push constants, call vkCmdDispatch
        std::function<void(VkCommandBuffer cmd,
                           ComputePipeline& pipeline,
                           uint32_t frameIndex)> dispatchCallback;
    };
    /**
     * I have to separate the compute pipelines from the graphic pipelines (Pipeline) because
     * Pipeline assumes too much, like assuming that i'll have render pass.
     * */
    class ComputePipeline {
    public:
        ComputePipeline(VkDevice device,
                        VmaAllocator  allocator,
                        const ComputePipelineConfig& config,
                        VkPipelineLayout pipelineLayout,
                        VkDescriptorSetLayout descriptorSetLayout);
        ~ComputePipeline();
        // Non-copyable
        ComputePipeline(const ComputePipeline&) = delete;
        ComputePipeline& operator=(const ComputePipeline&) = delete;
        /**
        * Binds the pipeline, that's before we draw.
        * */
        void Bind(VkCommandBuffer cmd) const;
        void Dispatch(VkCommandBuffer cmd, uint32_t frameIndex);
        VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const;
        void DispatchRaw(VkCommandBuffer cmd,
                                         uint32_t x, uint32_t y, uint32_t z) const;
        VkDescriptorSet    AllocateDescriptorSet();
        VkPipeline GetPipeline() const { return pipeline; }
        VkDevice GetDevice() const {return device;}
        VmaAllocator GetAllocator()const {return allocator;}
        VkPipelineLayout GetPipelineLayout()const {return pipelineLayout;}

    private:
        /**
         * Vk device, not owned by the device
         * */
        VkDevice device = VK_NULL_HANDLE;
        /**
         * Vma allocator. Not owned by this class.
         * */
        VmaAllocator allocator = VK_NULL_HANDLE;
        /**
         * Vulkan pipeline object, owned by the compute pipeline
         * */
        VkPipeline pipeline = VK_NULL_HANDLE;
        /**
         * Varies from pipeline to pipeline
         * */
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        /**
         * Varies from pipeline to pipeline
         * */
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        /**
         * Each specific pipeline has it's own descriptor pool, created in the constructor,
         * with the size specified at ComputePipelineConfig.
         * */
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        /**
         * Compute behaves differently from graphics pipeline. While the graphics
         * pipeline manages descriptor sets per-renderable that doesn't happen with
         * compute, they are pre-allocated in the case of compute.
         * */
        utils::RingBuffer<VkDescriptorSet> descriptorSets;
        /**
         * The callback comes from the config.
         * */
        std::function<void(VkCommandBuffer, ComputePipeline&, uint32_t)> dispatchCallback;
        /**
         * Helper function to create the shader module for compute.
         * */
        VkShaderModule CreateShaderModule(const std::vector<uint8_t>& data);

    };
}

#endif //KRAKATOA_COMPUTE_PIPELINE_H
