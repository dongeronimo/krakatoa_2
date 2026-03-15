#include "depth_deprojection_config.h"
#include <array>
#include "CDO.h"
#include "vk_debug.h"
#include "concatenate.h"

using namespace graphics;
/**
 * Type of the intrinsics buffer
 * */
struct IntrinsicsUbo_t {
    float fx;
    float fy;
    float cx;
    float cy;
};

ComputePipelineConfig graphics::DepthDeprojectConfig(VmaAllocator allocator) {
    //TODO deprojection: create the descriptor sets

    ComputePipelineConfig config;
    config.shaderName = "depth_deproject";
    config.descriptorPoolSizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  MAX_FRAMES_IN_FLIGHT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  MAX_FRAMES_IN_FLIGHT * 2},
    };

    config.dispatchCallback = [=](VkCommandBuffer cmd,
                                  ComputePipeline& pipeline,
                                  uint32_t frameIndex,
                                  CDO& cdo) {
        if(pipeline.HasBuffer("depth_deprojection_intrinsics_0")) {
            //Create the things here
            for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
                     z d//                VkBufferCreateInfo bufferInfo{};
//                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//                bufferInfo.size  = sizeof(IntrinsicsUbo_t);
//                bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
//
//                VmaAllocationCreateInfo allocInfo{};
//                allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
//                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
//                          | VMA_ALLOCATION_CREATE_MAPPED_BIT; // persistent mapping
//
//                VmaAllocationInfo allocResult{};
//                VkBuffer intrinsicsBuffer;
//                VmaAllocation intrinsicsAllocation;
//                vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &intrinsicsBuffer, &intrinsicsAllocation, &allocResult);
//                void* intrinsicsMappedPtr = allocResult.pMappedData; // always valid, no need to map/unmap
//                auto b_n = Concatenate("depth_deprojection_intrinsics_", i);
//                pipeline.AddBuffer(b_n,intrinsicsBuffer, intrinsicsAllocation, intrinsicsMappedPtr);
            }
        }
//        //TODO deprojection: if the buffers werent created yet, create them
//        if(pipeline.HasBuffer("depth_deprojection_intrinsics_0"))
//        {
//            //TODO deprojection: create the intrinsics buffer, it's map, etc...;
//            for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++){
//                /* I have to lazily create the buffers because it's only here, now that I have
//                 * enough data to do so.*/
//                //TODO deprojection: create the buffer for the intrinsics
//                VkBufferCreateInfo bufferInfo{};
//                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//                bufferInfo.size  = sizeof(IntrinsicsUbo_t);
//                bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
//
//                VmaAllocationCreateInfo allocInfo{};
//                allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
//                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
//                          | VMA_ALLOCATION_CREATE_MAPPED_BIT; // persistent mapping
//
//                VmaAllocationInfo allocResult{};
//                VkBuffer intrinsicsBuffer;
//                VmaAllocation intrinsicsAllocation;
//                vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &intrinsicsBuffer, &intrinsicsAllocation, &allocResult);
//                void* intrinsicsMappedPtr = allocResult.pMappedData; // always valid, no need to map/unmap
//                auto b_n = Concatenate("depth_deprojection_intrinsics_", i);
//                pipeline.AddBuffer(b_n,intrinsicsBuffer, intrinsicsAllocation, intrinsicsMappedPtr);
//                //TODO deprojection: update info for the intrinsics descriptor set
//                VkDescriptorSet ds = pipeline.GetDescriptorSet(i);
//                // binding 0 — intrinsics UBO
//                VkDescriptorBufferInfo uboInfo{};
//                uboInfo.buffer = intrinsicsBuffer;
//                uboInfo.offset = 0;
//                uboInfo.range  = VK_WHOLE_SIZE;
//                //TODO deprojection: update info for the depth buffer
//                VkDescriptorBufferInfo depthInfo{};
//                depthInfo.buffer = ???;
//                depthInfo.offset = 0;
//                depthInfo.range = VK_WHOLE_SIZE;
//            }
//        }
//        //TODO deprojection: update the descriptor sets with new data
//        VkDescriptorSet ds = pipeline.GetDescriptorSet(frameIndex);
//        // binding 0 — intrinsics UBO
//        VkDescriptorBufferInfo uboInfo{};
//        uboInfo.buffer = intrinsicsUBO;
//        uboInfo.offset = 0;
//        uboInfo.range  = VK_WHOLE_SIZE;
//
//        // binding 1 — depth SSBO
//        VkDescriptorBufferInfo depthInfo{};
//        depthInfo.buffer = depthSSBO;
//        depthInfo.offset = 0;
//        depthInfo.range  = VK_WHOLE_SIZE;
//
//        // binding 2 — output SSBO
//        VkDescriptorBufferInfo outInfo{};
//        outInfo.buffer = outputSSBO;
//        outInfo.offset = 0;
//        outInfo.range  = VK_WHOLE_SIZE;
//        VkWriteDescriptorSet writes[3]{};
//        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
//                     ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                     nullptr, &uboInfo, nullptr};
//        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
//                     ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
//                     nullptr, &depthInfo, nullptr};
//        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
//                     ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
//                     nullptr, &outInfo, nullptr};
//
//        vkUpdateDescriptorSets(pipeline.GetDevice(), 3, writes, 0, nullptr);
//
//        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
//                                pipeline.GetPipelineLayout(), 0, 1, &ds, 0, nullptr);
//
//        pipeline.DispatchRaw(cmd,
//                             (width  + 15) / 16,
//                             (height + 15) / 16,
//                             1);
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
