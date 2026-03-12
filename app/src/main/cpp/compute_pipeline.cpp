#include "compute_pipeline.h"

using namespace graphics;

ComputePipeline::ComputePipeline(VkDevice device, VmaAllocator allocator,
                                 const ComputePipelineConfig &config,
                                 VkPipelineLayout pipelineLayout,
                                 VkDescriptorSetLayout descriptorSetLayout) {
    VkComputePipelineCreateInfo info{};
    info.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage  = shaderStageInfo; // VK_SHADER_STAGE_COMPUTE_BIT
    info.layout = pipelineLayout;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
}

void ComputePipeline::Bind(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

void ComputePipeline::Dispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z) const {

}

VkDescriptorSet ComputePipeline::AllocateDescriptorSet() {
    return nullptr;
}
