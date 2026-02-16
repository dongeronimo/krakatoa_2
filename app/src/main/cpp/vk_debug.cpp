#include "vk_debug.h"
#include "android_log.h"

static PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectName = nullptr;
static PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginDebugUtilsLabel = nullptr;
static PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndDebugUtilsLabel = nullptr;

namespace graphics {
namespace debug {

void Initialize(VkInstance instance) {
    pfnSetDebugUtilsObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
    pfnCmdBeginDebugUtilsLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
    pfnCmdEndDebugUtilsLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));

    if (pfnSetDebugUtilsObjectName) {
        LOGI("Debug utils: object naming loaded");
    } else {
        LOGW("Debug utils: vkSetDebugUtilsObjectNameEXT not available");
    }
    if (pfnCmdBeginDebugUtilsLabel && pfnCmdEndDebugUtilsLabel) {
        LOGI("Debug utils: command buffer labels loaded");
    } else {
        LOGW("Debug utils: command buffer labels not available");
    }
}

void SetObjectName(VkDevice device, uint64_t objectHandle,
                   VkObjectType objectType, const std::string& name) {
    if (!pfnSetDebugUtilsObjectName) return;

    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = objectHandle;
    nameInfo.pObjectName = name.c_str();

    pfnSetDebugUtilsObjectName(device, &nameInfo);
}


void SetFenceName(VkDevice device, VkFence object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_FENCE, name);
}

void SetSemaphoreName(VkDevice device, VkSemaphore object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_SEMAPHORE, name);
}

void SetBufferName(VkDevice device, VkBuffer object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_BUFFER, name);
}

void SetImageName(VkDevice device, VkImage object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_IMAGE, name);
}

void SetImageViewName(VkDevice device, VkImageView object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_IMAGE_VIEW, name);
}

void SetRenderPassName(VkDevice device, VkRenderPass object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_RENDER_PASS, name);
}

void SetFramebufferName(VkDevice device, VkFramebuffer object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_FRAMEBUFFER, name);
}

void SetPipelineName(VkDevice device, VkPipeline object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_PIPELINE, name);
}

void SetDescriptorPoolName(VkDevice device, VkDescriptorPool object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_DESCRIPTOR_POOL, name);
}

void SetDescriptorSetName(VkDevice device, VkDescriptorSet object, const std::string& name) {
    SetObjectName(device, reinterpret_cast<uint64_t>(object),
                  VK_OBJECT_TYPE_DESCRIPTOR_SET, name);
}

void BeginLabel(VkCommandBuffer cmd, const std::string& name,
                float r, float g, float b, float a) {
    if (!pfnCmdBeginDebugUtilsLabel) return;

    VkDebugUtilsLabelEXT labelInfo{};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = name.c_str();
    labelInfo.color[0] = r;
    labelInfo.color[1] = g;
    labelInfo.color[2] = b;
    labelInfo.color[3] = a;

    pfnCmdBeginDebugUtilsLabel(cmd, &labelInfo);
}

void EndLabel(VkCommandBuffer cmd) {
    if (!pfnCmdEndDebugUtilsLabel) return;
    pfnCmdEndDebugUtilsLabel(cmd);
}

} // namespace debug
} // namespace graphics
