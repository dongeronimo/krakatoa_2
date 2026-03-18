#include "voxelization_op.h"
#include <cassert>
#include "compute_pipeline.h"
#include "android_log.h"

using namespace graphics;

VoxelizationOp::VoxelizationOp(const InitContext& ctx)
    : ComputeOperation(ctx, "Voxelization") {}

// ── Initialization ──────────────────────────────────────────────────────
void VoxelizationOp::Initialize() {
    assert(!initialized && "VoxelizationOp already initialized");
    assert(volumeView != VK_NULL_HANDLE && "Must SetVolumeImageView() before Initialize()");

    // 1. Descriptor set layout: 1 storage buffer + 1 storage image
    std::vector<VkDescriptorSetLayoutBinding> bindings(2);
    // binding 0: positions SSBO (from upstream deprojection)
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

    descriptorSetLayout = CreateDescriptorSetLayout(bindings);

    // 2. Pipeline layout with push constants
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(PushConstant);
    pipelineLayout = CreatePipelineLayout(descriptorSetLayout, &pushConstant);

    // 3. Build the compute pipeline
    ComputePipelineConfig config;
    config.shaderName = "voxelization";
    config.descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  MAX_FRAMES_IN_FLIGHT},
    };
    pipeline = std::make_unique<ComputePipeline>(
        device, allocator, config, pipelineLayout, descriptorSetLayout);

    initialized = true;
    LOGI("VoxelizationOp initialized");
}

// ── Per-frame execution ─────────────────────────────────────────────────
void VoxelizationOp::Execute(VkCommandBuffer cmd, uint32_t frameIndex) {
    assert(initialized);
    assert(positionSource != nullptr && "Must SetPositionSource() before Execute()");

    // Get the position buffer from the upstream operation
    VkBuffer positionsBuffer = positionSource->GetOutputBuffer(frameIndex);
    assert(positionsBuffer != VK_NULL_HANDLE);

    VkDescriptorSet ds = pipeline->GetDescriptorSet(frameIndex);

    // Descriptor writes
    VkDescriptorBufferInfo posInfo{};
    posInfo.buffer = positionsBuffer;
    posInfo.offset = 0;
    posInfo.range  = VK_WHOLE_SIZE;

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

    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    // Bind pipeline and descriptors
    pipeline->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &ds, 0, nullptr);

    // Push constants
    PushConstant pc{};
    pc.positionCount = positionCount_;
    pc.scale         = scale_;
    pc.volumeSize    = volumeSize_;
    vkCmdPushConstants(cmd, pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstant), &pc);

    // Dispatch: 1D, one thread per position, workgroup size 256
    pipeline->DispatchRaw(cmd,
                          (positionCount_ + 255) / 256,
                          1,
                          1);
}

// ── Setters ─────────────────────────────────────────────────────────────
void VoxelizationOp::SetPositionSource(ComputeOperation* source) {
    positionSource = source;
}

void VoxelizationOp::SetVolumeImageView(VkImageView view) {
    volumeView = view;
}

void VoxelizationOp::SetScale(float scale) {
    scale_ = scale;
}

void VoxelizationOp::SetVolumeSize(uint32_t size) {
    volumeSize_ = size;
}

void VoxelizationOp::SetPositionCount(uint32_t count) {
    positionCount_ = count;
}
