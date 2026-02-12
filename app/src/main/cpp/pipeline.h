#ifndef KRAKATOA_PIPELINE_H
#define KRAKATOA_PIPELINE_H
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>
namespace graphics {

    class RenderPass;

    /**
     * Configuration for the variable parts of a graphics pipeline.
     * Fields have sensible defaults for a typical opaque 3D pipeline.
     * Create custom configs by modifying the defaults or use the
     * factory functions below.
     */
    struct PipelineConfig {
        // --- Shaders ---
        std::string vertexShader;
        std::string fragmentShader;

        // --- Rasterizer ---
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        VkCullModeFlagBits cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        float lineWidth = 1.0f;

        // --- Depth / Stencil ---
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        bool stencilTestEnable = false;

        // --- Color blending (per attachment) ---
        bool blendEnable = false;
        VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
        VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;
        VkColorComponentFlags colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        // --- Input assembly ---
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool primitiveRestartEnable = false;
    };

    // --- Config factories ---

    /** Opaque unshaded: depth test+write, no blending, backface culling */
    PipelineConfig UnshadedOpaqueConfig();

    /** Translucent: depth test (no write), alpha blending, no culling */
    PipelineConfig TranslucentConfig();

    /** Wireframe: no depth write, no blending, line mode */
    PipelineConfig WireframeConfig();

    /**
     * A Vulkan graphics pipeline built from a PipelineConfig.
     *
     * Fixed aspects: dynamic viewport/scissor, vertex layout (pos+normal+uv),
     * no multisampling.
     * Variable aspects come from PipelineConfig.
     *
     * Usage:
     *   Pipeline pipeline(renderPass, device, UnshadedOpaqueConfig(), pipelineLayout);
     *   // in render loop:
     *   pipeline.Bind(cmd);
     *   vkCmdDraw(cmd, ...);
     */
    class Pipeline {
    public:
        Pipeline(RenderPass* renderPass,
                 VkDevice device,
                 const PipelineConfig& config,
                 VkPipelineLayout pipelineLayout);
        ~Pipeline();

        // Non-copyable
        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        void Bind(VkCommandBuffer cmd) const;
        VkPipeline GetPipeline() const { return pipeline; }

    private:
        VkDevice device;
        VkPipeline pipeline = VK_NULL_HANDLE;

        VkShaderModule CreateShaderModule(const std::vector<uint8_t>& data);
    };
}
#endif //KRAKATOA_PIPELINE_H