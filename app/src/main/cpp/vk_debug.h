#ifndef KRAKATOA_VK_DEBUG_H
#define KRAKATOA_VK_DEBUG_H
#include <vulkan/vulkan.h>
#include <string>
#include "concatenate.h"
namespace graphics {
namespace debug {

    /// Load debug function pointers. Call once after vkCreateInstance.
    void Initialize(VkInstance instance);

    /// Name a Vulkan object (generic).
    void SetObjectName(VkDevice device, uint64_t objectHandle,
                       VkObjectType objectType, const std::string& name);

    /// Convenience overloads for each object type the project uses.
    void SetObjectName(VkDevice device, VkFence object, const std::string& name);
    void SetObjectName(VkDevice device, VkSemaphore object, const std::string& name);
    void SetObjectName(VkDevice device, VkBuffer object, const std::string& name);
    void SetObjectName(VkDevice device, VkImage object, const std::string& name);
    void SetObjectName(VkDevice device, VkImageView object, const std::string& name);
    void SetObjectName(VkDevice device, VkRenderPass object, const std::string& name);
    void SetObjectName(VkDevice device, VkFramebuffer object, const std::string& name);
    void SetObjectName(VkDevice device, VkPipeline object, const std::string& name);

    /// Begin a debug label region on a command buffer (visible in RenderDoc).
    void BeginLabel(VkCommandBuffer cmd, const std::string& name,
                    float r = 0.0f, float g = 1.0f, float b = 0.0f, float a = 1.0f);

    /// End the current debug label region.
    void EndLabel(VkCommandBuffer cmd);

} // namespace debug
} // namespace graphics

#endif //KRAKATOA_VK_DEBUG_H
