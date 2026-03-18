#include "depth_deprojection_op.h"
#include <cassert>
#include <cstring>
#include "compute_pipeline.h"
#include "vk_debug.h"
#include "concatenate.h"
#include "android_log.h"

using namespace graphics;

DepthDeprojectionOp::DepthDeprojectionOp(const InitContext& ctx)
    : ComputeOperation(ctx, "DepthDeprojection") {}

DepthDeprojectionOp::~DepthDeprojectionOp() {
    // Destroy output buffers
    for (uint32_t i = 0; i < outputBuffers.Size(); ++i) {
        if (outputBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, outputBuffers[i], outputAllocations[i]);
        }
    }
    // Destroy intrinsics buffers
    for (uint32_t i = 0; i < intrinsicsBuffers.Size(); ++i) {
        if (intrinsicsBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, intrinsicsBuffers[i], intrinsicsAllocations[i]);
        }
    }
}

// ── Initialization ──────────────────────────────────────────────────────
void DepthDeprojectionOp::Initialize() {
    assert(!initialized && "DepthDeprojectionOp already initialized");
    assert(width > 0 && height > 0 && "Must SetDimensions() before Initialize()");

    // 1. Create descriptor set layout: 3 storage buffers
    std::vector<VkDescriptorSetLayoutBinding> bindings(3);
    // binding 0: intrinsics SSBO
    bindings[0].binding            = 0;
    bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount    = 1;
    bindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    // binding 1: depth input SSBO
    bindings[1].binding            = 1;
    bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount    = 1;
    bindings[1].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;
    // binding 2: output positions SSBO
    bindings[2].binding            = 2;
    bindings[2].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount    = 1;
    bindings[2].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    descriptorSetLayout = CreateDescriptorSetLayout(bindings);

    // 2. Create pipeline layout with push constants
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(PushConstant);
    pipelineLayout = CreatePipelineLayout(descriptorSetLayout, &pushConstant);

    // 3. Build the compute pipeline via ComputePipelineConfig
    ComputePipelineConfig config;
    config.shaderName = "deprojection";
    config.descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT * 3},
    };
    pipeline = std::make_unique<ComputePipeline>(
        device, allocator, config, pipelineLayout, descriptorSetLayout);

    // 4. Create our GPU resources
    CreateOutputBuffers();
    CreateIntrinsicsBuffers();

    initialized = true;
    LOGI("DepthDeprojectionOp initialized (%ux%u)", width, height);
}

// ── Per-frame execution ─────────────────────────────────────────────────
void DepthDeprojectionOp::Execute(VkCommandBuffer cmd, uint32_t frameIndex) {
    assert(initialized);
    assert(currentDepthBuffer != VK_NULL_HANDLE && "Must SetDepthBuffer() before Execute()");

    // Update intrinsics for this frame (host-visible, persistent mapping)
    IntrinsicsUbo intrinsics{ fx_, fy_, cx_, cy_ };
    memcpy(intrinsicsMappedPtrs[frameIndex], &intrinsics, sizeof(IntrinsicsUbo));

    // Get the descriptor set for this frame
    VkDescriptorSet ds = pipeline->GetDescriptorSet(frameIndex);

    // Descriptor writes: intrinsics, depth input, output positions
    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = intrinsicsBuffers[frameIndex];
    uboInfo.offset = 0;
    uboInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo depthInfo{};
    depthInfo.buffer = currentDepthBuffer;
    depthInfo.offset = 0;
    depthInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo outInfo{};
    outInfo.buffer = outputBuffers[frameIndex];
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

    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);

    // Bind pipeline and descriptors
    pipeline->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &ds, 0, nullptr);

    // Push constants: viewInverse + dimensions
    PushConstant pc{};
    memcpy(pc.viewInverse, viewInverse_.data(), sizeof(float) * 16);
    pc.width  = width;
    pc.height = height;
    vkCmdPushConstants(cmd, pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstant), &pc);

    // Dispatch: 2D grid, 16x16 workgroups (matches shader local_size)
    pipeline->DispatchRaw(cmd,
                          (width  + 15) / 16,
                          (height + 15) / 16,
                          1);
}

// ── Barrier: compute → compute + vertex ─────────────────────────────────
void DepthDeprojectionOp::InsertPostBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                             | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);
}

// ── Setters ─────────────────────────────────────────────────────────────
void DepthDeprojectionOp::SetDepthBuffer(VkBuffer depthSSBO) {
    currentDepthBuffer = depthSSBO;
}

void DepthDeprojectionOp::SetIntrinsics(float fx, float fy, float cx, float cy) {
    fx_ = fx; fy_ = fy; cx_ = cx; cy_ = cy;
}

void DepthDeprojectionOp::SetViewInverse(const std::array<float,16>& viewInv) {
    viewInverse_ = viewInv;
}

void DepthDeprojectionOp::SetDimensions(uint32_t w, uint32_t h) {
    width = w;
    height = h;
}

// ── Output accessor ─────────────────────────────────────────────────────
VkBuffer DepthDeprojectionOp::GetOutputBuffer(uint32_t frameIndex) const {
    return outputBuffers[frameIndex];
}

// ── Internal: create ring-buffered output position buffers ──────────────
void DepthDeprojectionOp::CreateOutputBuffers() {
    size_t sizeInBytes = width * height * sizeof(float) * 4; // vec4 per pixel

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size  = sizeInBytes;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkBuffer buf = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkResult r = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                                     &buf, &alloc, nullptr);
        assert(r == VK_SUCCESS && "Failed to allocate deprojection output buffer");

        outputBuffers[i] = buf;
        outputAllocations[i] = alloc;

        debug::SetBufferName(device, buf,
                             Concatenate("DepthDeprojectOutput[", i, "]"));
    }
}

// ── Internal: create ring-buffered intrinsics SSBOs ─────────────────────
void DepthDeprojectionOp::CreateIntrinsicsBuffers() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size  = sizeof(IntrinsicsUbo);
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                        | VMA_ALLOCATION_CREATE_MAPPED_BIT; // persistent mapping

        VmaAllocationInfo allocResult{};
        VkBuffer buf = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                        &buf, &alloc, &allocResult);

        intrinsicsBuffers[i] = buf;
        intrinsicsAllocations[i] = alloc;
        intrinsicsMappedPtrs[i] = allocResult.pMappedData;

        debug::SetBufferName(device, buf,
                             Concatenate("DepthDeprojectIntrinsics[", i, "]"));
    }
}
