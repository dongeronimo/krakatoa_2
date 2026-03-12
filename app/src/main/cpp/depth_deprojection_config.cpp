#include "depth_deprojection_config.h"
#include <array>
using namespace graphics;

ComputePipelineConfig
DepthDeprojectConfig(VkBuffer intrinsicsUBO, VkBuffer depthSSBO, VkBuffer outputSSBO,
                     uint32_t width, uint32_t height) {
    ComputePipelineConfig config;
    config.shaderName = "depth_deproject";
    config.descriptorPoolSizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  MAX_FRAMES_IN_FLIGHT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  MAX_FRAMES_IN_FLIGHT * 2},
    };
    config.dispatchCallback = [=](VkCommandBuffer cmd,
                                  ComputePipeline& pipeline,
                                  uint32_t frameIndex) {
        VkDescriptorSet ds = pipeline.GetDescriptorSet(frameIndex);
        // binding 0 — intrinsics UBO
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = intrinsicsUBO;
        uboInfo.offset = 0;
        uboInfo.range  = VK_WHOLE_SIZE;

        // binding 1 — depth SSBO
        VkDescriptorBufferInfo depthInfo{};
        depthInfo.buffer = depthSSBO;
        depthInfo.offset = 0;
        depthInfo.range  = VK_WHOLE_SIZE;

        // binding 2 — output SSBO
        VkDescriptorBufferInfo outInfo{};
        outInfo.buffer = outputSSBO;
        outInfo.offset = 0;
        outInfo.range  = VK_WHOLE_SIZE;
        VkWriteDescriptorSet writes[3]{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                     nullptr, &uboInfo, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                     nullptr, &depthInfo, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                     nullptr, &outInfo, nullptr};

        vkUpdateDescriptorSets(pipeline.GetDevice(), 3, writes, 0, nullptr);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline.GetPipelineLayout(), 0, 1, &ds, 0, nullptr);

        pipeline.DispatchRaw(cmd,
                             (width  + 15) / 16,
                             (height + 15) / 16,
                             1);
    };
    return config;
}

VkPipelineLayout DepthDeprojectionPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(float) * 16 + // matrix
                              sizeof(uint32_t) * 2; // width, height

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    VkPipelineLayout pipelineLayout;
    auto result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
    assert(result == VK_SUCCESS);
    return pipelineLayout;
}

VkDescriptorSetLayout DepthDeprojectionDescriptorSetLayout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

// binding 0: UBO (intrinsics + dimensions)
    bindings[0].binding            = 0;
    bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount    = 1;
    bindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

// binding 1: depth buffer
    bindings[1].binding            = 1;
    bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount    = 1;
    bindings[1].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

// binding 2: output positions
    bindings[2].binding            = 2;
    bindings[2].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount    = 1;
    bindings[2].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    dslInfo.pBindings    = bindings.data();

    VkDescriptorSetLayout descriptorSetLayout;
    vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &descriptorSetLayout);

    VkResult result = vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &descriptorSetLayout);
    assert(result == VK_SUCCESS);

    return descriptorSetLayout;
}
