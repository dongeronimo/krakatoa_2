#include "tsdf_fusion_op.h"
#include <cassert>
#include <cstring>
#include "compute_pipeline.h"
#include "tsdf_volume.h"
#include "vk_debug.h"
#include "concatenate.h"
#include "android_log.h"

using namespace graphics;

TsdfFusionOp::TsdfFusionOp(const InitContext& ctx)
    : ComputeOperation(ctx, "TsdfFusion") {}

TsdfFusionOp::~TsdfFusionOp() {
    // Destroy owned intrinsics buffers
    for (uint32_t i = 0; i < intrinsicsBuffers.Size(); ++i) {
        if (intrinsicsBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, intrinsicsBuffers[i], intrinsicsAllocations[i]);
        }
    }
}

// ── Initialization ──────────────────────────────────────────────────────
void TsdfFusionOp::Initialize() {
    assert(!initialized && "TsdfFusionOp already initialized");
    assert(volumeView != VK_NULL_HANDLE && "Must SetVolumeImageView() before Initialize()");

    // 1. Descriptor set layout: 1 storage image + 2 storage buffers
    std::vector<VkDescriptorSetLayoutBinding> bindings(3);
    // binding 0: TSDF volume (r32ui storage image)
    bindings[0].binding            = 0;
    bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount    = 1;
    bindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    // binding 1: depth buffer SSBO
    bindings[1].binding            = 1;
    bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount    = 1;
    bindings[1].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;
    // binding 2: intrinsics SSBO
    bindings[2].binding            = 2;
    bindings[2].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount    = 1;
    bindings[2].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    descriptorSetLayout = CreateDescriptorSetLayout(bindings);

    // 2. Pipeline layout with push constants
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(PushConstant);
    pipelineLayout = CreatePipelineLayout(descriptorSetLayout, &pushConstant);

    // 3. Build the compute pipeline
    ComputePipelineConfig config;
    config.shaderName = "tsdf_fusion";
    config.descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT * 2},
    };
    pipeline = std::make_unique<ComputePipeline>(
        device, allocator, config, pipelineLayout, descriptorSetLayout);

    // 4. Create intrinsics ring buffers
    CreateIntrinsicsBuffers();

    initialized = true;
    LOGI("TsdfFusionOp initialized (pushConstant size: %zu bytes)", sizeof(PushConstant));
}

// ── Per-frame execution ─────────────────────────────────────────────────
void TsdfFusionOp::Execute(VkCommandBuffer cmd, uint32_t frameIndex) {
    assert(initialized);
    assert(currentDepthBuffer != VK_NULL_HANDLE && "Must SetDepthBuffer() before Execute()");

    // Update intrinsics SSBO for this frame
    auto* mapped = static_cast<IntrinsicsUbo*>(intrinsicsMappedPtrs[frameIndex]);
    mapped->fx = fx_;
    mapped->fy = fy_;
    mapped->cx = cx_;
    mapped->cy = cy_;

    VkDescriptorSet ds = pipeline->GetDescriptorSet(frameIndex);

    // ── Descriptor writes ───────────────────────────────────────────────

    // binding 0: TSDF volume image
    VkDescriptorImageInfo volumeInfo{};
    volumeInfo.imageView   = volumeView;
    volumeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    volumeInfo.sampler     = VK_NULL_HANDLE;

    // binding 1: depth buffer
    VkDescriptorBufferInfo depthInfo{};
    depthInfo.buffer = currentDepthBuffer;
    depthInfo.offset = 0;
    depthInfo.range  = VK_WHOLE_SIZE;

    // binding 2: intrinsics
    VkDescriptorBufferInfo intrInfo{};
    intrInfo.buffer = intrinsicsBuffers[frameIndex];
    intrInfo.offset = 0;
    intrInfo.range  = sizeof(IntrinsicsUbo);

    VkWriteDescriptorSet writes[3]{};
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
    writes[1].pBufferInfo     = &depthInfo;

    writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet          = ds;
    writes[2].dstBinding      = 2;
    writes[2].dstArrayElement = 0;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo     = &intrInfo;

    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);

    // Bind pipeline and descriptors
    pipeline->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &ds, 0, nullptr);

    // Push constants
    PushConstant pc{};
    memcpy(pc.viewMatrix, viewMatrix_.data(), sizeof(float) * 16);
    pc.truncationDist = truncationDist_;
    pc.scale          = scale_;
    pc.volumeSize     = volumeSize_;
    pc.depthWidth     = depthWidth_;
    pc.depthHeight    = depthHeight_;
    pc.maxWeight      = maxWeight_;
    vkCmdPushConstants(cmd, pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstant), &pc);

    // Dispatch: 3D grid, workgroup size 8x8x4 = 256 threads, over volumeSize³ voxels
    pipeline->DispatchRaw(cmd,
                          (volumeSize_ + 7) / 8,
                          (volumeSize_ + 7) / 8,
                          (volumeSize_ + 3) / 4);
}

// ── Setters ─────────────────────────────────────────────────────────────
void TsdfFusionOp::SetVolumeImageView(VkImageView view) {
    volumeView = view;
}

void TsdfFusionOp::SetDepthBuffer(VkBuffer depthSSBO) {
    currentDepthBuffer = depthSSBO;
}

void TsdfFusionOp::SetIntrinsics(float fx, float fy, float cx, float cy) {
    fx_ = fx; fy_ = fy; cx_ = cx; cy_ = cy;
}

void TsdfFusionOp::SetViewMatrix(const std::array<float,16>& viewMat) {
    viewMatrix_ = viewMat;
}

void TsdfFusionOp::SetDepthDimensions(uint32_t w, uint32_t h) {
    depthWidth_ = w;
    depthHeight_ = h;
}

void TsdfFusionOp::SetScale(float scale) {
    scale_ = scale;
}

void TsdfFusionOp::SetVolumeSize(uint32_t size) {
    volumeSize_ = size;
}

void TsdfFusionOp::SetTruncationDistance(float dist) {
    truncationDist_ = dist;
}

void TsdfFusionOp::SetMaxWeight(float maxWeight) {
    maxWeight_ = maxWeight;
}

// ── Internal: create intrinsics ring buffers ────────────────────────────
void TsdfFusionOp::CreateIntrinsicsBuffers() {
    for (uint32_t i = 0; i < intrinsicsBuffers.Size(); ++i) {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size  = sizeof(IntrinsicsUbo);
        bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                        | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo mapInfo{};
        vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                        &intrinsicsBuffers[i], &intrinsicsAllocations[i], &mapInfo);
        intrinsicsMappedPtrs[i] = mapInfo.pMappedData;
        debug::SetBufferName(device, intrinsicsBuffers[i],
                           Concatenate("TsdfFusion:Intrinsics:", i));
    }
}
