#ifndef KRAKATOA_SWAPCHAIN_RENDER_PASS_H
#define KRAKATOA_SWAPCHAIN_RENDER_PASS_H
#include "render_pass.h"
#include "vk_mem_alloc.h"
#include <vector>
namespace graphics {
    /**
     * Render pass that writes to swapchain images for final presentation.
     * Color attachment ends in PRESENT_SRC_KHR.
     *
     * Receives swapchain image views from outside (the swapchain owner).
     * Owns: depth image, N framebuffers (one per swapchain image).
     * Call Recreate() after swapchain recreation (resize/rotation/surface lost).
     */
    class SwapchainRenderPass : public RenderPass {
    public:
        SwapchainRenderPass(VkDevice device,
                            VmaAllocator allocator,
                            VkFormat swapchainFormat,
                            VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT);
        ~SwapchainRenderPass() override;

        /// Create framebuffers for the given swapchain image views.
        /// Call this after swapchain creation and after every recreation.
        void Recreate(const std::vector<VkImageView>& swapchainImageViews,
                      VkExtent2D extent);

        VkFramebuffer GetFramebuffer(uint32_t imageIndex) const {
            return framebuffers[imageIndex];
        }

        VkExtent2D GetExtent() const { return extent; }
        uint32_t GetFramebufferCount() const {
            return static_cast<uint32_t>(framebuffers.size());
        }

    private:
        VmaAllocator allocator = VK_NULL_HANDLE;


        VkFormat swapchainFormat;
        VkFormat depthFormat;
        VkExtent2D extent = {0, 0};

        // Depth (owned)
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;

        // One framebuffer per swapchain image (owned)
        std::vector<VkFramebuffer> framebuffers;

        void CreateRenderPass();
        void CreateDepthImage();
        void CreateFramebuffers(const std::vector<VkImageView>& swapchainImageViews);
        void DestroyDepthImage();
        void DestroyFramebuffers();
    };
}
#endif //KRAKATOA_SWAPCHAIN_RENDER_PASS_H