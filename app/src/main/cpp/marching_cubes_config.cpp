#include "marching_cubes_config.h"
#include <array>
#include <cstring>
#include <cassert>
#include "CDO.h"
#include "gpu_mesh.h"
#include "vk_debug.h"
#include "concatenate.h"
#include "marching_cubes_tables.h"
#include "voxel_volume.h"

using namespace graphics;

struct MarchingCubesPushConstant {
    uint32_t cutoff;
    float    scale;        // same scale used in voxelization (100.0 for 1cm)
    float    maxDistance;   // max edge length in voxel units
    uint32_t volumeSizeX;
    uint32_t volumeSizeY;
    uint32_t volumeSizeZ;
    uint32_t maxVertices;
    uint32_t maxIndices;
};

ComputePipelineConfig graphics::MarchingCubesConfig(VmaAllocator allocator) {
    ComputePipelineConfig config;
    config.shaderName = "marching_cubes";
    config.descriptorPoolSizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  MAX_FRAMES_IN_FLIGHT},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT * 5},
    };

    config.dispatchCallback = [=](VkCommandBuffer cmd,
                                  ComputePipeline& pipeline,
                                  uint32_t frameIndex,
                                  CDO& cdo) {
        // Lazily create the lookup table SSBOs (once)
        if (!pipeline.HasBuffer("mc_edge_table")) {
            // Edge table: 256 ints
            {
                VkBufferCreateInfo bufInfo{};
                bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufInfo.size  = sizeof(mc::edgeTable);
                bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                | VMA_ALLOCATION_CREATE_MAPPED_BIT;

                VmaAllocationInfo mapInfo{};
                VkBuffer buf;
                VmaAllocation alloc;
                vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &buf, &alloc, &mapInfo);
                memcpy(mapInfo.pMappedData, mc::edgeTable, sizeof(mc::edgeTable));
                pipeline.AddBuffer("mc_edge_table", buf, alloc);
                debug::SetBufferName(pipeline.GetDevice(), buf, "MC:EdgeTable");
            }

            // Triangle table: 256 * 16 ints
            {
                VkBufferCreateInfo bufInfo{};
                bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufInfo.size  = sizeof(mc::triTable);
                bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                | VMA_ALLOCATION_CREATE_MAPPED_BIT;

                VmaAllocationInfo mapInfo{};
                VkBuffer buf;
                VmaAllocation alloc;
                vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &buf, &alloc, &mapInfo);
                memcpy(mapInfo.pMappedData, mc::triTable, sizeof(mc::triTable));
                pipeline.AddBuffer("mc_tri_table", buf, alloc);
                debug::SetBufferName(pipeline.GetDevice(), buf, "MC:TriTable");
            }
        }

        VkImageView volumeView = cdo.GetVkImageView(CDO::Keys::volume_image_view);
        VkBuffer vertexBuf     = cdo.GetVkBuffer(CDO::Keys::mc_vertex_buffer);
        VkBuffer indexBuf      = cdo.GetVkBuffer(CDO::Keys::mc_index_buffer);
        VkBuffer counterBuf    = cdo.GetVkBuffer(CDO::Keys::mc_counter_buffer);

        VkBuffer edgeTableBuf  = pipeline.GetBuffer("mc_edge_table");
        VkBuffer triTableBuf   = pipeline.GetBuffer("mc_tri_table");

        VkDescriptorSet ds = pipeline.GetDescriptorSet(frameIndex);

        // binding 0: volume image
        VkDescriptorImageInfo volumeInfo{};
        volumeInfo.imageView   = volumeView;
        volumeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        volumeInfo.sampler     = VK_NULL_HANDLE;

        // binding 1: edge table
        VkDescriptorBufferInfo edgeInfo{};
        edgeInfo.buffer = edgeTableBuf;
        edgeInfo.offset = 0;
        edgeInfo.range  = VK_WHOLE_SIZE;

        // binding 2: tri table
        VkDescriptorBufferInfo triInfo{};
        triInfo.buffer = triTableBuf;
        triInfo.offset = 0;
        triInfo.range  = VK_WHOLE_SIZE;

        // binding 3: vertex output
        VkDescriptorBufferInfo vertInfo{};
        vertInfo.buffer = vertexBuf;
        vertInfo.offset = 0;
        vertInfo.range  = VK_WHOLE_SIZE;

        // binding 4: index output
        VkDescriptorBufferInfo idxInfo{};
        idxInfo.buffer = indexBuf;
        idxInfo.offset = 0;
        idxInfo.range  = VK_WHOLE_SIZE;

        // binding 5: atomic counters
        VkDescriptorBufferInfo ctrInfo{};
        ctrInfo.buffer = counterBuf;
        ctrInfo.offset = 0;
        ctrInfo.range  = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[6]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = ds;
        writes[0].dstBinding      = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo      = &volumeInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = ds;
        writes[1].dstBinding      = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo     = &edgeInfo;

        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = ds;
        writes[2].dstBinding      = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo     = &triInfo;

        writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = ds;
        writes[3].dstBinding      = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo     = &vertInfo;

        writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = ds;
        writes[4].dstBinding      = 4;
        writes[4].dstArrayElement = 0;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].pBufferInfo     = &idxInfo;

        writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet          = ds;
        writes[5].dstBinding      = 5;
        writes[5].dstArrayElement = 0;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].pBufferInfo     = &ctrInfo;

        vkUpdateDescriptorSets(pipeline.GetDevice(), 6, writes, 0, nullptr);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline.GetPipelineLayout(), 0, 1, &ds, 0, nullptr);

        // Push constants
        MarchingCubesPushConstant pc{};
        pc.cutoff     = cdo.GetUint32(CDO::Keys::mc_cutoff);
        pc.scale      = cdo.GetFloat(CDO::Keys::voxel_scale);
        pc.maxDistance = cdo.GetFloat(CDO::Keys::mc_max_distance);
        pc.volumeSizeX = VoxelVolume::VOLUME_SIZE_X;
        pc.volumeSizeY = VoxelVolume::VOLUME_SIZE_Y;
        pc.volumeSizeZ = VoxelVolume::VOLUME_SIZE_Z;
        pc.maxVertices = cdo.GetUint32(CDO::Keys::mc_max_vertices);
        pc.maxIndices  = cdo.GetUint32(CDO::Keys::mc_max_indices);

        vkCmdPushConstants(cmd, pipeline.GetPipelineLayout(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(MarchingCubesPushConstant), &pc);

        // Dispatch: 3D, workgroup size 4³, over (volumeSize-1) cells per axis
        uint32_t cellsX = VoxelVolume::VOLUME_SIZE_X - 1;
        uint32_t cellsY = VoxelVolume::VOLUME_SIZE_Y - 1;
        uint32_t cellsZ = VoxelVolume::VOLUME_SIZE_Z - 1;
        pipeline.DispatchRaw(cmd,
                             (cellsX + 3) / 4,
                             (cellsY + 3) / 4,
                             (cellsZ + 3) / 4);
    };
    return config;
}

VkPipelineLayout graphics::MarchingCubesPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(MarchingCubesPushConstant);

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

VkDescriptorSetLayout graphics::MarchingCubesDescriptorSetLayout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};

    // 0: volume image
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    // 1: edge table SSBO
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    // 2: tri table SSBO
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    // 3: vertex output SSBO
    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    // 4: index output SSBO
    bindings[4].binding         = 4;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    // 5: atomic counters SSBO
    bindings[5].binding         = 5;
    bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    dslInfo.pBindings    = bindings.data();

    VkDescriptorSetLayout descriptorSetLayout;
    VkResult result = vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &descriptorSetLayout);
    assert(result == VK_SUCCESS);

    return descriptorSetLayout;
}
