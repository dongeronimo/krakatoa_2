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
    void SetFenceName(VkDevice device, VkFence object, const std::string& name);
    void SetSemaphoreName(VkDevice device, VkSemaphore object, const std::string& name);
    void SetBufferName(VkDevice device, VkBuffer object, const std::string& name);
    void SetImageName(VkDevice device, VkImage object, const std::string& name);
    void SetImageViewName(VkDevice device, VkImageView object, const std::string& name);
    void SetRenderPassName(VkDevice device, VkRenderPass object, const std::string& name);
    void SetFramebufferName(VkDevice device, VkFramebuffer object, const std::string& name);
    void SetPipelineName(VkDevice device, VkPipeline object, const std::string& name);
    void SetDescriptorPoolName(VkDevice device, VkDescriptorPool object, const std::string& name);
    void SetDescriptorSetName(VkDevice device, VkDescriptorSet object, const std::string& name);

    /// Begin a debug label region on a command buffer (visible in RenderDoc).
    void BeginLabel(VkCommandBuffer cmd, const std::string& name,
                    float r = 0.0f, float g = 1.0f, float b = 0.0f, float a = 1.0f);

    /// End the current debug label region.
    void EndLabel(VkCommandBuffer cmd);

} // namespace debug
} // namespace graphics

#endif //KRAKATOA_VK_DEBUG_H
