#ifndef KRAKATOA_IMGUI_INTEGRATION_H
#define KRAKATOA_IMGUI_INTEGRATION_H

#include <vulkan/vulkan.h>

namespace imgui_integration {

    struct InitInfo {
        VkInstance       instance;
        VkPhysicalDevice physicalDevice;
        VkDevice         device;
        uint32_t         queueFamily;
        VkQueue          graphicsQueue;
        VkRenderPass     renderPass;
        uint32_t         imageCount;       // swapchain image count
        float            displayWidth;
        float            displayHeight;
    };

    /// Initialize ImGui context, Vulkan backend, descriptor pool, and font atlas.
    /// Call once after Vulkan device/swapchain/render-pass are ready.
    void Init(const InitInfo& info);

    /// Tear down ImGui and free Vulkan resources.
    void Shutdown();

    /// Call when the surface size changes (orientation, resize).
    void OnSurfaceChanged(float width, float height);

    /// Forward Android touch events from JNI.
    /// action: 0 = DOWN, 1 = MOVE, 2 = UP (matches VulkanSurfaceView.kt encoding).
    void OnTouchEvent(float x, float y, int action);

    /// Begin a new ImGui frame. Call once per frame, before any ImGui widget code.
    /// deltaSeconds comes from FrameTimer.
    void NewFrame(float deltaSeconds);

    /// Finalize the ImGui frame and record draw commands into the command buffer.
    /// Must be called inside an active render pass (swapchain pass).
    void Render(VkCommandBuffer cmd);

    /// Register a user texture so it can be drawn with ImGui::Image().
    /// Returns an ImTextureID (which is a VkDescriptorSet under the hood).
    VkDescriptorSet AddTexture(VkSampler sampler, VkImageView imageView,
                               VkImageLayout layout);

    /// Release a texture descriptor set previously created with AddTexture().
    void RemoveTexture(VkDescriptorSet descriptorSet);

    /// True after Init() and before Shutdown().
    bool IsInitialized();
}

#endif //KRAKATOA_IMGUI_INTEGRATION_H
