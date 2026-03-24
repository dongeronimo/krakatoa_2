#include "marching_cubes_op.h"
#include <cassert>
#include <cstring>
#include "compute_pipeline.h"
#include "gpu_mesh.h"
#include "tsdf_volume.h"
#include "marching_cubes_tables.h"
#include "vk_debug.h"
#include "android_log.h"

using namespace graphics;

MarchingCubesOp::MarchingCubesOp(const InitContext& ctx)
    : ComputeOperation(ctx, "MarchingCubes") {}

MarchingCubesOp::~MarchingCubesOp() {
    // Destroy owned lookup table buffers
    if (edgeTableBuffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, edgeTableBuffer, edgeTableAllocation);
    if (triTableBuffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, triTableBuffer, triTableAllocation);
}

// ── Initialization ──────────────────────────────────────────────────────
void MarchingCubesOp::Initialize() {
    assert(!initialized && "MarchingCubesOp already initialized");
    assert(volumeView != VK_NULL_HANDLE && "Must SetVolumeImageView() before Initialize()");
    assert(outputMesh != nullptr && "Must SetOutputMesh() before Initialize()");

    // 1. Descriptor set layout: 1 storage image + 5 storage buffers
    std::vector<VkDescriptorSetLayoutBinding> bindings(6);
    // binding 0: volume image
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    // binding 1: edge table SSBO
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;
    // binding 2: tri table SSBO
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].pImmutableSamplers = nullptr;
    // binding 3: vertex output SSBO
    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[3].pImmutableSamplers = nullptr;
    // binding 4: index output SSBO
    bindings[4].binding         = 4;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[4].pImmutableSamplers = nullptr;
    // binding 5: atomic counter SSBO
    bindings[5].binding         = 5;
    bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[5].pImmutableSamplers = nullptr;

    descriptorSetLayout = CreateDescriptorSetLayout(bindings);

    // 2. Pipeline layout with push constants
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(PushConstant);
    pipelineLayout = CreatePipelineLayout(descriptorSetLayout, &pushConstant);

    // 3. Build the compute pipeline
    ComputePipelineConfig config;
    config.shaderName = "marching_cubes";
    config.descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT * 5},
    };
    pipeline = std::make_unique<ComputePipeline>(
        device, allocator, config, pipelineLayout, descriptorSetLayout);

    // 4. Create lookup table buffers (populated once, read-only after)
    CreateLookupTables();

    initialized = true;
    LOGI("MarchingCubesOp initialized");
}

// ── Per-frame execution ─────────────────────────────────────────────────
void MarchingCubesOp::Execute(VkCommandBuffer cmd, uint32_t frameIndex) {
    assert(initialized);

    VkDescriptorSet ds = pipeline->GetDescriptorSet(frameIndex);

    // ── Descriptor writes ───────────────────────────────────────────────

    // binding 0: volume image
    VkDescriptorImageInfo volumeInfo{};
    volumeInfo.imageView   = volumeView;
    volumeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    volumeInfo.sampler     = VK_NULL_HANDLE;

    // binding 1: edge table
    VkDescriptorBufferInfo edgeInfo{};
    edgeInfo.buffer = edgeTableBuffer;
    edgeInfo.offset = 0;
    edgeInfo.range  = VK_WHOLE_SIZE;

    // binding 2: tri table
    VkDescriptorBufferInfo triInfo{};
    triInfo.buffer = triTableBuffer;
    triInfo.offset = 0;
    triInfo.range  = VK_WHOLE_SIZE;

    // binding 3: vertex output
    VkDescriptorBufferInfo vertInfo{};
    vertInfo.buffer = outputMesh->GetVertexStorageBuffer();
    vertInfo.offset = 0;
    vertInfo.range  = VK_WHOLE_SIZE;

    // binding 4: index output
    VkDescriptorBufferInfo idxInfo{};
    idxInfo.buffer = outputMesh->GetIndexStorageBuffer();
    idxInfo.offset = 0;
    idxInfo.range  = VK_WHOLE_SIZE;

    // binding 5: atomic counters
    VkDescriptorBufferInfo ctrInfo{};
    ctrInfo.buffer = outputMesh->GetCounterBuffer();
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

    vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);

    // ── Bind and dispatch ───────────────────────────────────────────────
    pipeline->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &ds, 0, nullptr);

    // Push constants
    PushConstant pc{};
    pc.scale      = scale_;
    pc.maxDistance = maxDistance_;
    pc.volumeSize = TsdfVolume::VOLUME_SIZE;
    pc.maxVertices = outputMesh->GetMaxVertices();
    pc.maxIndices  = outputMesh->GetMaxIndices();
    pc.minWeight  = minWeight_;
    vkCmdPushConstants(cmd, pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstant), &pc);

    // Dispatch: 3D grid, workgroup size 4x4x4, over (volumeSize-1)^3 cells
    uint32_t cells = TsdfVolume::VOLUME_SIZE - 1;
    pipeline->DispatchRaw(cmd,
                          (cells + 3) / 4,
                          (cells + 3) / 4,
                          (cells + 3) / 4);
}

// ── Barrier: compute write → vertex input + transfer ────────────────────
void MarchingCubesOp::InsertPostBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
                          | VK_ACCESS_INDEX_READ_BIT
                          | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
                             | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);
}

// ── Setters ─────────────────────────────────────────────────────────────
void MarchingCubesOp::SetVolumeImageView(VkImageView view) {
    volumeView = view;
}

void MarchingCubesOp::SetOutputMesh(GpuMesh* mesh) {
    outputMesh = mesh;
}

void MarchingCubesOp::SetScale(float scale) {
    scale_ = scale;
}

void MarchingCubesOp::SetMaxDistance(float maxDist) {
    maxDistance_ = maxDist;
}

void MarchingCubesOp::SetMinWeight(float minWeight) {
    minWeight_ = minWeight;
}

// ── Internal: create lookup table buffers ───────────────────────────────
void MarchingCubesOp::CreateLookupTables() {
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
        vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                        &edgeTableBuffer, &edgeTableAllocation, &mapInfo);
        memcpy(mapInfo.pMappedData, mc::edgeTable, sizeof(mc::edgeTable));
        debug::SetBufferName(device, edgeTableBuffer, "MC:EdgeTable");
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
        vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                        &triTableBuffer, &triTableAllocation, &mapInfo);
        memcpy(mapInfo.pMappedData, mc::triTable, sizeof(mc::triTable));
        debug::SetBufferName(device, triTableBuffer, "MC:TriTable");
    }
}
