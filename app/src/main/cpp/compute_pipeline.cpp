#include "compute_pipeline.h"
#include <cassert>
#include "concatenate.h"
#include "asset_loader.h"
#include "android_log.h"
#include "vk_debug.h"
using namespace graphics;
static std::vector<uint8_t> LoadComputeShader(const std::string& name) {
    auto path = Concatenate("shaders/", name, ".comp.spv");
    auto data = io::AssetLoader::loadFile(path);
    if (data.empty()) {
        LOGE("FATAL: compute shader '%s' missing or unreadable", path.c_str());
        std::abort();
    }
    return data;
}
VkShaderModule ComputePipeline::CreateShaderModule(const std::vector<uint8_t>& data) {
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = data.size();
    info.pCode    = reinterpret_cast<const uint32_t*>(data.data());

    VkShaderModule mod;
    VkResult r = vkCreateShaderModule(device, &info, nullptr, &mod);
    if (r != VK_SUCCESS) {
        LOGE("FATAL: vkCreateShaderModule failed (%d), shader size %zu", r, data.size());
        std::abort();
    }
    return mod;
}
ComputePipeline::ComputePipeline(VkDevice device, VmaAllocator allocator,
                                 const ComputePipelineConfig &config,
                                 VkPipelineLayout pipelineLayout,
                                 VkDescriptorSetLayout descriptorSetLayout)
                                 :device(device), allocator(allocator),
                                 pipelineLayout(pipelineLayout),
                                 descriptorSetLayout(descriptorSetLayout),
                                 dispatchCallback(config.dispatchCallback)
{
    //sanity checks
    assert(!config.shaderName.empty());
    assert(pipelineLayout != VK_NULL_HANDLE);
    assert(descriptorSetLayout != VK_NULL_HANDLE);
    //shader
    auto src = LoadComputeShader(config.shaderName);
    VkShaderModule shaderModule = CreateShaderModule(src);
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName  = "main";
    // ── pipeline ──
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage  = stageInfo;
    pipelineInfo.layout = pipelineLayout;

    VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                          &pipelineInfo, nullptr, &pipeline);
    assert(r == VK_SUCCESS);
    vkDestroyShaderModule(device, shaderModule, nullptr); // done after pipeline creation
    debug::SetPipelineName(device, pipeline,
                           Concatenate("ComputePipeline:", config.shaderName));

    // ── descriptor pool ──
    assert(!config.descriptorPoolSizes.empty());
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets       = MAX_COMPUTE_DESCRIPTOR_SETS;
    poolInfo.poolSizeCount = static_cast<uint32_t>(config.descriptorPoolSizes.size());
    poolInfo.pPoolSizes    = config.descriptorPoolSizes.data();
    r = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
    assert(r == VK_SUCCESS);

    // ── pre-allocate one descriptor set per frame in flight ──
    // Unlike graphics (which is per-Renderable), compute sets are per-pipeline.
    // The caller updates bindings each frame via vkUpdateDescriptorSets.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &descriptorSetLayout;

        VkDescriptorSet& ds = descriptorSets.Next();
        r = vkAllocateDescriptorSets(device, &allocInfo, &ds);
        assert(r == VK_SUCCESS);

        debug::SetDescriptorSetName(device, ds,
                                    Concatenate("ComputeDescSet[", i, "]:", config.shaderName));
    }

    LOGI("ComputePipeline created (%s)", config.shaderName.c_str());
}

void ComputePipeline::Bind(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

void ComputePipeline::Dispatch(VkCommandBuffer cmd, uint32_t frameIndex) {
    assert(dispatchCallback);
    Bind(cmd);
    dispatchCallback(cmd, *this, frameIndex);
    descriptorSets.Next();
}

VkDescriptorSet ComputePipeline::AllocateDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &descriptorSetLayout;

    VkDescriptorSet ds;
    VkResult r = vkAllocateDescriptorSets(device, &allocInfo, &ds);
    assert(r == VK_SUCCESS);
    return ds;
}

ComputePipeline::~ComputePipeline() {
    // Pool destruction implicitly frees all descriptor sets
    if (descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        LOGI("ComputePipeline destroyed");
    }
}

void ComputePipeline::DispatchRaw(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z) const {
    vkCmdDispatch(cmd, x, y, z);
}

VkDescriptorSet ComputePipeline::GetDescriptorSet(uint32_t frameIndex) const {
    return descriptorSets[frameIndex];
}