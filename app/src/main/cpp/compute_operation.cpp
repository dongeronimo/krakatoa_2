#include "compute_operation.h"
#include "compute_pipeline.h"  // full definition needed for unique_ptr<ComputePipeline> destructor
#include <cassert>
#include "android_log.h"

using namespace graphics;

ComputeOperation::ComputeOperation(const InitContext& ctx, const std::string& name)
    : device(ctx.device), allocator(ctx.allocator), name(name) {
    assert(device != VK_NULL_HANDLE);
    assert(allocator != VK_NULL_HANDLE);
}

ComputeOperation::~ComputeOperation() {
    // Destroy pipeline layout and descriptor set layout.
    // The ComputePipeline destructor handles its own cleanup (pipeline object,
    // descriptor pool, etc.), so we just need to destroy the layouts we created.
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }
    LOGI("ComputeOperation '%s' destroyed", name.c_str());
}

// ── Default barrier: compute write → compute read ───────────────────────
void ComputeOperation::InsertPostBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);
}

// ── Default output interface: nothing ───────────────────────────────────
VkBuffer ComputeOperation::GetOutputBuffer(uint32_t /*frameIndex*/) const {
    return VK_NULL_HANDLE;
}

VkImageView ComputeOperation::GetOutputImageView() const {
    return VK_NULL_HANDLE;
}

// ── Helper: create a descriptor set layout ──────────────────────────────
VkDescriptorSetLayout ComputeOperation::CreateDescriptorSetLayout(
    const std::vector<VkDescriptorSetLayoutBinding>& bindings) {

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings    = bindings.data();

    VkDescriptorSetLayout layout;
    VkResult r = vkCreateDescriptorSetLayout(device, &info, nullptr, &layout);
    assert(r == VK_SUCCESS);
    return layout;
}

// ── Helper: create a pipeline layout ────────────────────────────────────
VkPipelineLayout ComputeOperation::CreatePipelineLayout(
    VkDescriptorSetLayout dsLayout,
    const VkPushConstantRange* pushConstant) {

    VkPipelineLayoutCreateInfo info{};
    info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount         = 1;
    info.pSetLayouts            = &dsLayout;
    info.pushConstantRangeCount = pushConstant ? 1 : 0;
    info.pPushConstantRanges    = pushConstant;

    VkPipelineLayout layout;
    VkResult r = vkCreatePipelineLayout(device, &info, nullptr, &layout);
    assert(r == VK_SUCCESS);
    return layout;
}
