#ifndef KRAKATOA_PIPELINE_H
#define KRAKATOA_PIPELINE_H
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <functional>
#include <unordered_map>
#include "ring_buffer.h"
#include <vk_mem_alloc.h>

namespace graphics {
    class Renderable;
    class RDO;
    class RenderPass;
    class Pipeline;

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
        // --- Descriptor pool sizes (config-driven, each pipeline declares what it needs) ---
        std::vector<VkDescriptorPoolSize> descriptorPoolSizes;
        // --- Actual drawing, varies between the pipelines bc each pipeline uses different fields and send different data to the shaders
        std::function<void(VkCommandBuffer cmd,
                RDO* rdo, Renderable* obj, Pipeline& pipeline, uint32_t frameIndex)> renderCallback;
    };
    /**
     * The uniform buffer for an object in a pipeline.
     * While the amount of buffers will be the same, based on the ring buffer,
     * the size won't, since each pipeline will have it's own uniforms.
     *
     * The death counter thing is to do some garbage collection based on last time used.
     * THe idea is that the pipeline will sweep its uniforms buffers table, decrementing the counter.
     * Any buffer with counter == 0 will be deleted. Every time a buffer is used the counter increases.
     * I need this because if i don't delete unused buffers the gpu will eventually run out of memory.
     * */
    struct UniformBuffer {
        utils::RingBuffer<VkBuffer> gpuBuffer;
        utils::RingBuffer<VkBuffer> stagingBuffer;
        utils::RingBuffer<VmaAllocation> gpuBufferAllocation;
        utils::RingBuffer<VmaAllocation> stagingBufferAllocation;
        utils::RingBuffer<void*> mappedData;
        utils::RingBuffer<VkDescriptorSet> descriptorSets;
        size_t size;
        uint64_t id;
        uint32_t deathCounter;
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
                 VmaAllocator allocator,
                 const PipelineConfig& config,
                 VkPipelineLayout pipelineLayout,
                 VkDescriptorSetLayout descriptorSetLayout);
        ~Pipeline();

        // Non-copyable
        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;
        /**
         * Binds the pipeline, that's before we draw.
         * */
        void Bind(VkCommandBuffer cmd) const;
        /**
         * Draw the object using the callback defined in PipelineConfig
         * */
        void Draw(VkCommandBuffer cmd, RDO* rdo, Renderable* renderable,
                  uint32_t frameIndex);
        VkPipeline GetPipeline() const { return pipeline; }
        VkDevice GetDevice() const {return device;}
        VmaAllocator GetAllocator()const {return allocator;}
        VkPipelineLayout GetPipelineLayout()const {return pipelineLayout;}
        VkDescriptorSet AllocateDescriptorSet();
        std::shared_ptr<UniformBuffer> GetUniformBuffer(uint64_t id) {
            if(uniformBuffers.count(id) == 0)
                return nullptr;
            else
                return uniformBuffers[id];
        }
        void AddUniformBuffer(uint64_t id, std::shared_ptr<UniformBuffer> b);
    private:
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::function<void(VkCommandBuffer cmd, RDO* rdo, Renderable* obj, Pipeline& pipeline, uint32_t frameIndex)> renderCallback;
        VkShaderModule CreateShaderModule(const std::vector<uint8_t>& data);
        std::unordered_map<uint64_t, std::shared_ptr<UniformBuffer>> uniformBuffers;
        void DecreaseDeathCounter();
    };
}
#endif //KRAKATOA_PIPELINE_H