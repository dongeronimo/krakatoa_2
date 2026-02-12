#ifndef KRAKATOA_RENDER_PASS_H
#define KRAKATOA_RENDER_PASS_H
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
namespace graphics {
    class RenderPass {
    public:
        virtual ~RenderPass() = default;

        void Begin(VkCommandBuffer cmd,
                   VkFramebuffer framebuffer,
                   VkExtent2D extent);
        void End(VkCommandBuffer cmd);

        VkRenderPass GetRenderPass() const { return renderPass; }
        void setClearColor(float r, float g, float b, float a) {
            clearValues[0].color = {{r, g, b, a}};
        }
    protected:
        VkDevice device = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;

        /// Subclasses define their own clear values
        std::vector<VkClearValue> clearValues;

        void DestroyRenderPass();
    };
}
#endif //KRAKATOA_RENDER_PASS_H