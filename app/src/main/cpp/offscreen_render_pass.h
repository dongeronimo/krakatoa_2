#ifndef KRAKATOA_OFFSCREEN_RENDER_PASS_H
#define KRAKATOA_OFFSCREEN_RENDER_PASS_H
#include "render_pass.h"
#include "vk_mem_alloc.h"
#include "ring_buffer.h"
namespace graphics {
    struct FrameResources {
        VkImage colorImage = VK_NULL_HANDLE;
        VmaAllocation colorAllocation = VK_NULL_HANDLE;
        VkImageView colorImageView = VK_NULL_HANDLE;
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };
    /**
     * Offscreen render pass that renders to its own color and depth textures.
     * The color attachment ends in SHADER_READ_ONLY_OPTIMAL so it can be
     * sampled in subsequent passes.
     *
     * Owns all its images, image views, and framebuffers (one set per frame in flight).
     * Call AdvanceFrame() at the start of each frame, then use GetFramebuffer()/
     * GetColorImageView() to access the current frame's resources.
     * Call Resize() to recreate at a different resolution.
     */
    class OffscreenRenderPass : public RenderPass {
    public:
        OffscreenRenderPass(VkDevice device,
                            VmaAllocator allocator,
                            uint32_t width,
                            uint32_t height,
                            VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                            VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT);
        ~OffscreenRenderPass() override;

        /// Advance to the next frame's resources. Call once at the start of each frame.
        void AdvanceFrame();

        /// Recreate images and framebuffers at new resolution.
        /// RenderPass object itself is NOT recreated (format-only contract).
        void Resize(uint32_t width, uint32_t height);

        /// Current frame's framebuffer (call AdvanceFrame() first)
        VkFramebuffer GetFramebuffer() const { return frameResources.Current().framebuffer; }
        /// Current frame's color image view, ready to be sampled
        VkImageView GetColorImageView() const { return frameResources.Current().colorImageView; }
        /// Current frame's depth image view
        VkImageView GetDepthImageView() const { return frameResources.Current().depthImageView; }

        VkExtent2D GetExtent() const { return {width, height}; }
        VkFormat GetColorFormat() const { return colorFormat; }
        VkFormat GetDepthFormat() const { return depthFormat; }

    private:
        VmaAllocator allocator = VK_NULL_HANDLE;

        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat colorFormat;
        VkFormat depthFormat;

        utils::RingBuffer<FrameResources> frameResources;

        void CreateRenderPass();
        void CreateImages();
        void CreateFramebuffers();
        void DestroyImages();
        void DestroyFramebuffers();

        VkImageView CreateImageView(VkImage image, VkFormat format,
                                    VkImageAspectFlags aspectFlags);
    };
}
#endif //KRAKATOA_OFFSCREEN_RENDER_PASS_H