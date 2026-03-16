#include "voxelization_config.h"
#include <array>
#include "CDO.h"

using namespace graphics;

struct VoxelizationPushConstant {
    uint32_t positionCount;
    float scale;         // meters → voxel units (100.0 = 1cm voxels)
    uint32_t volumeSizeX;
    uint32_t volumeSizeY;
    uint32_t volumeSizeZ;
};

ComputePipelineConfig graphics::VoxelizationConfig() {
    ComputePipelineConfig config;
    config.shaderName = "voxelization";
    config.descriptorPoolSizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  MAX_FRAMES_IN_FLIGHT},
    };

    config.dispatchCallback = [](VkCommandBuffer cmd,
                                 ComputePipeline& pipeline,
                                 uint32_t frameIndex,
                                 CDO& cdo) {
        VkBuffer positionsBuffer = cdo.GetVkBuffer(CDO::Keys::vec4_buffer);
        VkImageView volumeView  = cdo.GetVkImageView(CDO::Keys::volume_image_view);
        uint32_t positionCount   = cdo.GetUint32(CDO::Keys::position_count);

        VkDescriptorSet ds = pipeline.GetDescriptorSet(frameIndex);

        // binding 0 — positions SSBO (from deprojection output)
        VkDescriptorBufferInfo posInfo{};
        posInfo.buffer = positionsBuffer;
        posInfo.offset = 0;
        posInfo.range  = VK_WHOLE_SIZE;

        // binding 1 — 3D volume storage image
        VkDescriptorImageInfo volumeInfo{};
        volumeInfo.imageView   = volumeView;
        volumeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        volumeInfo.sampler     = VK_NULL_HANDLE; // storage image, no sampler

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet           = ds;
        writes[0].dstBinding       = 0;
        writes[0].dstArrayElement  = 0;
        writes[0].descriptorCount  = 1;
        writes[0].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo      = &posInfo;

        writes[1].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet           = ds;
        writes[1].dstBinding       = 1;
        writes[1].dstArrayElement  = 0;
        writes[1].descriptorCount  = 1;
        writes[1].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo       = &volumeInfo;

        vkUpdateDescriptorSets(pipeline.GetDevice(), 2, writes, 0, nullptr);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline.GetPipelineLayout(), 0, 1, &ds, 0, nullptr);

        // Push the position count, scale, and volume dimensions
        VoxelizationPushConstant pc{};
        pc.positionCount = positionCount;
        pc.scale = cdo.GetFloat(CDO::Keys::voxel_scale);
        pc.volumeSizeX = cdo.GetUint32(CDO::Keys::mc_volume_size_x);
        pc.volumeSizeY = cdo.GetUint32(CDO::Keys::mc_volume_size_y);
        pc.volumeSizeZ = cdo.GetUint32(CDO::Keys::mc_volume_size_z);
        vkCmdPushConstants(cmd, pipeline.GetPipelineLayout(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(VoxelizationPushConstant), &pc);

        // Dispatch: 1D, one thread per position, workgroup size 256
        pipeline.DispatchRaw(cmd,
                             (positionCount + 255) / 256,
                             1,
                             1);
    };
    return config;
}

VkPipelineLayout graphics::VoxelizationPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(VoxelizationPushConstant);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushConstant;

    VkPipelineLayout pipelineLayout;
    VkResult result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
    assert(result == VK_SUCCESS);
    return pipelineLayout;
}

VkDescriptorSetLayout graphics::VoxelizationDescriptorSetLayout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // binding 0: positions SSBO
    bindings[0].binding            = 0;
    bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount    = 1;
    bindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // binding 1: 3D volume storage image
    bindings[1].binding            = 1;
    bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount    = 1;
    bindings[1].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    dslInfo.pBindings    = bindings.data();

    VkDescriptorSetLayout descriptorSetLayout;
    VkResult result = vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &descriptorSetLayout);
    assert(result == VK_SUCCESS);

    return descriptorSetLayout;
}
