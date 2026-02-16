#include "pipeline.h"
#include "render_pass.h"
#include "asset_loader.h"
#include "vk_debug.h"
#include "android_log.h"
#include <cassert>
#include <array>
#include "renderable.h"
#include "rdo.h"
#include "concatenate.h"
#include "vk_debug.h"
#include <glm/gtc/type_ptr.hpp>
using namespace graphics;

template<typename T>
void createGpuUniformBuffers(
        VmaAllocator allocator,
        uint32_t count,
        utils::RingBuffer<VkBuffer>& gpuBuffers,
        utils::RingBuffer<VmaAllocation>& gpuAllocations)
{
    for (uint32_t i = 0; i < count; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(T);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkBuffer& buffer = gpuBuffers.Next();
        VmaAllocation& allocation = gpuAllocations.Next();

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                            &buffer, &allocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create GPU uniform buffer");
        }
    }
}
template<typename T>
void createStagingUniformBuffers(
        VmaAllocator allocator,
        uint32_t count,
        utils::RingBuffer<VkBuffer>& stagingBuffers,
        utils::RingBuffer<VmaAllocation>& stagingAllocations,
        utils::RingBuffer<void*>& mappedData)
{
    for (uint32_t i = 0; i < count; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(T);
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo mapInfo{};
        VkBuffer& buffer = stagingBuffers.Next();
        VmaAllocation& allocation = stagingAllocations.Next();

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                            &buffer, &allocation, &mapInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create staging uniform buffer");
        }

        void*& mapped = mappedData.Next();
        mapped = mapInfo.pMappedData;
    }
}
// ============================================================
// Config factories
// ============================================================

struct UnshadedOpaqueUniformBuffer {
    float model[16];
    float view[16];
    float projection[16];
    float color[4];
};
PipelineConfig graphics::UnshadedOpaqueConfig() {
    PipelineConfig config;
    config.vertexShader = "unshaded_opaque.vert";
    config.fragmentShader = "unshaded_opaque.frag";
    config.depthTestEnable = true;
    config.depthWriteEnable = true;
    config.blendEnable = false;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    config.descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_DESCRIPTOR_SETS_PER_POOL}
    };
    /**
     * Expects MODEL, VIEW, PROJECTION, COLOR
     * */
    config.renderCallback = [](VkCommandBuffer cmd, RDO* rdo, Renderable* obj, Pipeline& pipeline, uint32_t frameIndex){
        // TODO: Get the descriptor set for this object
        std::shared_ptr<UniformBuffer> uniformBuffer  = pipeline.GetUniformBuffer(obj->GetId());
        if(uniformBuffer == nullptr){
            //TODO: Create the buffers and store in the pipeline
            std::shared_ptr<UniformBuffer> ub = std::make_unique<UniformBuffer>();
            createGpuUniformBuffers<UnshadedOpaqueUniformBuffer>(pipeline.GetAllocator(),
                                    MAX_FRAMES_IN_FLIGHT, ub->gpuBuffer, ub->gpuBufferAllocation);
            createStagingUniformBuffers<UnshadedOpaqueUniformBuffer>(pipeline.GetAllocator(),
                                                                     MAX_FRAMES_IN_FLIGHT,
                                                                     ub->stagingBuffer,
                                                                     ub->stagingBufferAllocation,ub->mappedData);
            ub->size = sizeof(UnshadedOpaqueUniformBuffer);
            ub->id = obj->GetId();
            ub->deathCounter = 100;
            //Allocate one descriptor set per frame
            for(auto i=0; i<MAX_FRAMES_IN_FLIGHT;i++){
                VkDescriptorSet& ds = ub->descriptorSets.Next();
                ds = pipeline.AllocateDescriptorSet();
            }
            pipeline.AddUniformBuffer(obj->GetId(), ub);
            //Give a name to the relevant objects.
            for(auto i=0; i<MAX_FRAMES_IN_FLIGHT;i++){
                auto name1 = Concatenate("UnshadedOpaqueBuffer[", i, "]");
                debug::SetBufferName(pipeline.GetDevice(), ub->gpuBuffer[i], name1);
                auto name2 = Concatenate("UnshadedOpaqueBuffer(staging)[", i, "]");
                debug::SetBufferName(pipeline.GetDevice(), ub->stagingBuffer[i], name2);
                auto name3 = Concatenate("UnshadedOpaqueDescSet[", i, "]");
                debug::SetDescriptorSetName(pipeline.GetDevice(), ub->descriptorSets[i], name3);
            }
            uniformBuffer = ub;
        }
        // TODO: Fill it with new data
        // 1) fill the uniform buffer
        glm::mat4 model = rdo->GetMat4(RDO::MODEL_MAT);
        glm::mat4 view = rdo->GetMat4(RDO::VIEW_MAT);
        glm::mat4 proj = rdo->GetMat4(RDO::PROJ_MAT);
        glm::vec4 color = rdo->GetVec4(RDO::COLOR);
        UnshadedOpaqueUniformBuffer data{};
        memcpy(data.model, glm::value_ptr(model), sizeof(float) * 16);
        memcpy(data.view, glm::value_ptr(view), sizeof(float) * 16);
        memcpy(data.projection, glm::value_ptr(proj), sizeof(float) * 16);
        memcpy(data.color, glm::value_ptr(color), sizeof(float) * 4);
        //copy to the staging buffer
        memcpy(uniformBuffer->mappedData.Current(), &data, sizeof(UnshadedOpaqueUniformBuffer));
        //from the staging buffer to the gpuBuffer
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = uniformBuffer->size;
        vkCmdCopyBuffer(cmd, uniformBuffer->stagingBuffer.Current(), uniformBuffer->gpuBuffer.Current(), 1, &copyRegion);
        //Barrier to wait the copy - we need the fresh data.
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        barrier.buffer = uniformBuffer->gpuBuffer.Current();
        barrier.offset = 0;
        barrier.size = uniformBuffer->size;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 1, &barrier, 0, nullptr);
        // TODO: Draw

        // TODO: Advance buffers and increment the keepalive
        uniformBuffer->deathCounter++;
        uniformBuffer->gpuBuffer.Next();
        uniformBuffer->stagingBuffer.Next();
        uniformBuffer->mappedData.Next();
        uniformBuffer->descriptorSets.Next();
    };
    return config;
}

PipelineConfig graphics::TranslucentConfig() {
    PipelineConfig config;
    config.vertexShader = "translucent.vert";
    config.fragmentShader = "translucent.frag";
    config.depthTestEnable = true;
    config.depthWriteEnable = false;  // don't write depth for translucent
    config.blendEnable = true;
    config.descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_DESCRIPTOR_SETS_PER_POOL}
    };
    config.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    config.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.colorBlendOp = VK_BLEND_OP_ADD;
    config.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    config.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    config.alphaBlendOp = VK_BLEND_OP_ADD;
    config.cullMode = VK_CULL_MODE_NONE;
    config.renderCallback = [](VkCommandBuffer cmd, RDO* rdo, Renderable* obj, Pipeline& pipeline, uint32_t frameIndex){

    };
    return config;
}

PipelineConfig graphics::WireframeConfig() {
    PipelineConfig config;
    config.vertexShader = "wireframe.vert";
    config.fragmentShader = "wireframe.frag";
    config.polygonMode = VK_POLYGON_MODE_LINE;
    config.depthTestEnable = true;
    config.depthWriteEnable = false;
    config.blendEnable = false;
    config.cullMode = VK_CULL_MODE_NONE;
    config.lineWidth = 1.0f;
    config.descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_DESCRIPTOR_SETS_PER_POOL}
    };
    config.renderCallback = [](VkCommandBuffer cmd, RDO* rdo, Renderable* obj, Pipeline& pipeline, uint32_t frameIndex){

    };
    return config;
}

// ============================================================
// Shader loading
// ============================================================

static std::vector<uint8_t> LoadShaderBytes(const std::string& name) {
    auto filePath = Concatenate("shaders/", name, ".spv");
    return io::AssetLoader::loadFile(filePath);
}

VkShaderModule Pipeline::CreateShaderModule(const std::vector<uint8_t>& data) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = data.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(data.data());

    VkShaderModule shaderModule;
    VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    assert(result == VK_SUCCESS);
    return shaderModule;
}

// ============================================================
// Pipeline construction
// ============================================================

Pipeline::Pipeline(RenderPass* renderPass,
                   VkDevice device,
                   VmaAllocator allocator,
                   const PipelineConfig& config,
                   VkPipelineLayout pipelineLayout,
                   VkDescriptorSetLayout descriptorSetLayout)
        : device(device), allocator(allocator), pipelineLayout(pipelineLayout), descriptorSetLayout(descriptorSetLayout) {
    assert(this->pipelineLayout != VK_NULL_HANDLE);
    assert(this->descriptorSetLayout != VK_NULL_HANDLE);
    // --- Shader stages ---
    std::vector<uint8_t> vsSrc = LoadShaderBytes(config.vertexShader);
    std::vector<uint8_t> fsSrc = LoadShaderBytes(config.fragmentShader);
    VkShaderModule vs = CreateShaderModule(vsSrc);
    VkShaderModule fs = CreateShaderModule(fsSrc);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vs;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fs;
    fragStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStage, fragStage};

    // --- Vertex input (fixed: pos vec3 + normal vec3 + uv vec2) ---
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(float) * 8; // 3 + 3 + 2
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescs{};
    // position (vec3)
    attributeDescs[0].location = 0;
    attributeDescs[0].binding = 0;
    attributeDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescs[0].offset = 0;
    // normal (vec3)
    attributeDescs[1].location = 1;
    attributeDescs[1].binding = 0;
    attributeDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescs[1].offset = sizeof(float) * 3;
    // uv (vec2)
    attributeDescs[2].location = 2;
    attributeDescs[2].binding = 0;
    attributeDescs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescs[2].offset = sizeof(float) * 6;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributeDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();

    // --- Input assembly (from config) ---
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = config.topology;
    inputAssembly.primitiveRestartEnable = config.primitiveRestartEnable ? VK_TRUE : VK_FALSE;

    // --- Viewport / scissor (dynamic) ---
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    // actual values set dynamically via vkCmdSetViewport/vkCmdSetScissor

    // --- Rasterizer (from config) ---
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = config.lineWidth;

    // --- Multisampling (fixed: off) ---
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    // --- Depth / stencil (from config) ---
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = config.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = config.stencilTestEnable ? VK_TRUE : VK_FALSE;

    // --- Color blending (from config) ---
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = config.blendEnable ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
    colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
    colorBlendAttachment.colorBlendOp = config.colorBlendOp;
    colorBlendAttachment.srcAlphaBlendFactor = config.srcAlphaBlendFactor;
    colorBlendAttachment.dstAlphaBlendFactor = config.dstAlphaBlendFactor;
    colorBlendAttachment.alphaBlendOp = config.alphaBlendOp;
    colorBlendAttachment.colorWriteMask = config.colorWriteMask;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // --- Dynamic states (fixed: viewport + scissor) ---
    std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // --- Create graphics pipeline ---
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass->GetRenderPass();
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                               &pipelineInfo, nullptr, &pipeline);
    assert(result == VK_SUCCESS);
    debug::SetPipelineName(device, pipeline,
                         Concatenate("Pipeline:", config.vertexShader, "+", config.fragmentShader));

    // Cleanup shader modules (no longer needed after pipeline creation)
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);

    // --- Descriptor pool (config-driven sizes) ---
    assert(!config.descriptorPoolSizes.empty());
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = MAX_DESCRIPTOR_SETS_PER_POOL;
    poolInfo.poolSizeCount = static_cast<uint32_t>(config.descriptorPoolSizes.size());
    poolInfo.pPoolSizes = config.descriptorPoolSizes.data();
    result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
    assert(result == VK_SUCCESS);
    debug::SetDescriptorPoolName(device, descriptorPool,
                                 Concatenate("DescPool:", config.vertexShader, "+", config.fragmentShader));

    renderCallback = config.renderCallback;
    LOGI("Pipeline created (vs=%s, fs=%s)", config.vertexShader.c_str(),
         config.fragmentShader.c_str());
}

Pipeline::~Pipeline() {
    //TODO: Destroy the uniform buffers
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        LOGI("Pipeline destroyed");
    }
}

void Pipeline::Bind(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void Pipeline::Draw(VkCommandBuffer cmd, RDO *rdo, Renderable *renderable, uint32_t frameIndex) {
    renderCallback(cmd, rdo, renderable, *this, frameIndex);
}

VkDescriptorSet Pipeline::AllocateDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet;
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
    assert(result == VK_SUCCESS);
    return descriptorSet;
}

void Pipeline::AddUniformBuffer(uint64_t id, std::shared_ptr<UniformBuffer> b) {
    assert(uniformBuffers.count(id) == 0);
    uniformBuffers.insert({id, b});
}

void Pipeline::DecreaseDeathCounter() {
    std::vector<std::shared_ptr<UniformBuffer>> toDie;
    for(auto& [key, value] : uniformBuffers){
        value->deathCounter--;
        if(value->deathCounter <= 0){
            toDie.push_back(value);
        }
    }
    for(auto& dead:toDie){
        for (uint32_t i = 0; i < dead->gpuBuffer.Size(); i++) {
            vmaDestroyBuffer(allocator, dead->gpuBuffer[i], dead->gpuBufferAllocation[i]);
            vmaDestroyBuffer(allocator, dead->stagingBuffer[i], dead->stagingBufferAllocation[i]);
        }
        uniformBuffers.erase(dead->id);
    }
}