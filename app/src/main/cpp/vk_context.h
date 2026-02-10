#ifndef KRAKATOA_VK_CONTEXT_H
#define KRAKATOA_VK_CONTEXT_H
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include "queue_family_indices.h"
namespace graphics {
    class VkContext {
    public:
        VkContext();
        ~VkContext();
        bool Initialize();
        VkInstance GetInstance() const {return instance;}
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkDevice getDevice() const { return device; }
        VkQueue getGraphicsQueue() const { return graphicsQueue; }
        VkQueue getPresentQueue() const { return presentQueue; }
        VkQueue getComputeQueue() const { return computeQueue; }
        VkQueue getTransferQueue() const { return transferQueue; }
        QueueFamilyIndices getQueueFamilies() const { return queueFamilies; }


    private:
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        VkQueue computeQueue = VK_NULL_HANDLE;
        VkQueue transferQueue = VK_NULL_HANDLE;
        QueueFamilyIndices queueFamilies;
        bool createInstance();
        bool pickPhysicalDevice();
        bool createLogicalDevice();
        std::vector<const char*> getRequiredExtensions();
        std::vector<const char*> getRequiredLayers();
        std::vector<const char*> getRequiredDeviceExtensions();
        bool checkValidationLayerSupport();
        bool isDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    };
}


#endif //KRAKATOA_VK_CONTEXT_H
