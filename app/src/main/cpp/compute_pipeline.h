#ifndef KRAKATOA_COMPUTE_PIPELINE_H
#define KRAKATOA_COMPUTE_PIPELINE_H
#include <vulkan/vulkan.h>
#include <string>
#include <functional>
#include <vector>
#include "vk_mem_alloc.h"
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
        // Non-copyable
        ComputePipeline(const ComputePipeline&) = delete;
        ComputePipeline& operator=(const ComputePipeline&) = delete;
        /**
        * Binds the pipeline, that's before we draw.
        * */
        void Bind(VkCommandBuffer cmd) const;
        void Dispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z) const;
        VkDescriptorSet    AllocateDescriptorSet();
        VkPipeline GetPipeline() const { return pipeline; }
        VkDevice GetDevice() const {return device;}
        VmaAllocator GetAllocator()const {return allocator;}
        VkPipelineLayout GetPipelineLayout()const {return pipelineLayout;}

    private:
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    };
}

#endif //KRAKATOA_COMPUTE_PIPELINE_H
