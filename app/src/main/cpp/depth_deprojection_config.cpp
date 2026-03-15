#include "depth_deprojection_config.h"
#include <array>
#include <cstring>
#include "CDO.h"
#include "vk_debug.h"
#include "concatenate.h"

using namespace graphics;
/**
 * Type of the intrinsics buffer (matches shader binding 0 layout)
 * */
struct IntrinsicsUbo_t {
    float fx;
    float fy;
    float cx;
    float cy;
};

/**
 * Push constant struct (matches shader push_constant layout)
 * */
struct DepthDeprojectionPushConstant {
    float viewInverse[16]; // mat4
    uint32_t width;
    uint32_t height;
};

ComputePipelineConfig graphics::DepthDeprojectConfig(VmaAllocator allocator) {
    ComputePipelineConfig config;
    config.shaderName = "deprojection";
    config.descriptorPoolSizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  MAX_FRAMES_IN_FLIGHT * 3},
    };

    config.dispatchCallback = [=](VkCommandBuffer cmd,
                                  ComputePipeline& pipeline,
                                  uint32_t frameIndex,
                                  CDO& cdo) {
        // Lazily create the intrinsics buffers (one per frame in flight)
        if(!pipeline.HasBuffer("depth_deprojection_intrinsics_0")) {
            for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size  = sizeof(IntrinsicsUbo_t);
                bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                          | VMA_ALLOCATION_CREATE_MAPPED_BIT; // persistent mapping

                VmaAllocationInfo allocResult{};
                VkBuffer intrinsicsBuffer;
                VmaAllocation intrinsicsAllocation;
                vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &intrinsicsBuffer, &intrinsicsAllocation, &allocResult);
                void* intrinsicsMappedPtr = allocResult.pMappedData;
                auto b_n = Concatenate("depth_deprojection_intrinsics_", i);
                pipeline.AddBuffer(b_n, intrinsicsBuffer, intrinsicsAllocation, intrinsicsMappedPtr);

                debug::SetBufferName(pipeline.GetDevice(), intrinsicsBuffer,
                                     Concatenate("DepthDeprojectIntrinsics[", i, "]"));
            }
        }

        // Update intrinsics for this frame
        auto intrinsicsKey = Concatenate("depth_deprojection_intrinsics_", frameIndex);
        // Get the mapped pointer — we stored it when creating the buffer
        IntrinsicsUbo_t intrinsics{};
        intrinsics.fx = cdo.GetFloat(CDO::Keys::fx);
        intrinsics.fy = cdo.GetFloat(CDO::Keys::fy);
        intrinsics.cx = cdo.GetFloat(CDO::Keys::cx);
        intrinsics.cy = cdo.GetFloat(CDO::Keys::cy);
        // Copy to mapped memory via the stored buffer's mapped pointer
        void* mapped = pipeline.GetMappedMemory(intrinsicsKey);
        memcpy(mapped, &intrinsics, sizeof(IntrinsicsUbo_t));

        // Get the depth and output buffers from the CDO
        VkBuffer depthSSBO  = cdo.GetVkBuffer(CDO::Keys::uint16_buffer);
        VkBuffer outputSSBO = cdo.GetVkBuffer(CDO::Keys::vec4_buffer);
        VkBuffer intrinsicsBuffer = pipeline.GetBuffer(intrinsicsKey);

        // Update descriptor sets for this frame
        VkDescriptorSet ds = pipeline.GetDescriptorSet(frameIndex);

        // binding 0 — intrinsics SSBO
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = intrinsicsBuffer;
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
                     ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
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

        // Push constants: viewInverse + width + height
        DepthDeprojectionPushConstant pc{};
        auto& viewInv = cdo.GetMat4(CDO::Keys::view_inverse);
        memcpy(pc.viewInverse, viewInv.data(), sizeof(float) * 16);
        pc.width  = static_cast<uint32_t>(cdo.GetInt32(CDO::Keys::width));
        pc.height = static_cast<uint32_t>(cdo.GetInt32(CDO::Keys::height));

        vkCmdPushConstants(cmd, pipeline.GetPipelineLayout(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(DepthDeprojectionPushConstant), &pc);

        // Dispatch with ceil division to cover all pixels
        pipeline.DispatchRaw(cmd,
                             (pc.width  + 15) / 16,
                             (pc.height + 15) / 16,
                             1);
    };
    return config;
}

VkPipelineLayout graphics::DepthDeprojectionPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(DepthDeprojectionPushConstant);

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

VkDescriptorSetLayout graphics::DepthDeprojectionDescriptorSetLayout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

// binding 0: SSBO (intrinsics)
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
    VkResult result = vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &descriptorSetLayout);
    assert(result == VK_SUCCESS);

    return descriptorSetLayout;
}
