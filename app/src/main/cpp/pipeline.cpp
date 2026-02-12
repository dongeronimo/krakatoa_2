#include "pipeline.h"
#include "render_pass.h"
#include "asset_loader.h"
#include "concatenate.h"
#include "android_log.h"
#include <cassert>
#include <array>
using namespace graphics;

// ============================================================
// Config factories
// ============================================================

PipelineConfig graphics::UnshadedOpaqueConfig() {
    PipelineConfig config;
    config.vertexShader = "unshaded_opaque.vert";
    config.fragmentShader = "unshaded_opaque.frag";
    config.depthTestEnable = true;
    config.depthWriteEnable = true;
    config.blendEnable = false;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    return config;
}

PipelineConfig graphics::TranslucentConfig() {
    PipelineConfig config;
    config.vertexShader = "translucent.vert";
    config.fragmentShader = "translucent.frag";
    config.depthTestEnable = true;
    config.depthWriteEnable = false;  // don't write depth for translucent
    config.blendEnable = true;
    config.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    config.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.colorBlendOp = VK_BLEND_OP_ADD;
    config.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    config.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    config.alphaBlendOp = VK_BLEND_OP_ADD;
    config.cullMode = VK_CULL_MODE_NONE;
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
                   const PipelineConfig& config,
                   VkPipelineLayout pipelineLayout)
        : device(device) {

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

    // Cleanup shader modules (no longer needed after pipeline creation)
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);

    LOGI("Pipeline created (vs=%s, fs=%s)", config.vertexShader.c_str(),
         config.fragmentShader.c_str());
}

Pipeline::~Pipeline() {
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        LOGI("Pipeline destroyed");
    }
}

void Pipeline::Bind(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}