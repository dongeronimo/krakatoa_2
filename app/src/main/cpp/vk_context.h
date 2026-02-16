#ifndef KRAKATOA_VK_CONTEXT_H
#define KRAKATOA_VK_CONTEXT_H
#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <vector>
#include <optional>
#include "vk_mem_alloc.h"
#include "queue_family_indices.h"
namespace graphics {

    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    class VkContext {
    public:
        VkContext();
        ~VkContext();
        bool Initialize();
        bool CreateSurface(ANativeWindow* window);
        bool CreateSwapchain(uint32_t width, uint32_t height);
        bool RecreateSwapchain(uint32_t width, uint32_t height);

        VkInstance GetInstance() const { return instance; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkDevice GetDevice() const { return device; }
        VkQueue getGraphicsQueue() const { return graphicsQueue; }
        VkQueue getPresentQueue() const { return presentQueue; }
        VkQueue getComputeQueue() const { return computeQueue; }
        VkQueue getTransferQueue() const { return transferQueue; }
        QueueFamilyIndices getQueueFamilies() const { return queueFamilies; }
        VkSurfaceKHR getSurface() const { return surface; }
        VmaAllocator GetAllocator() const { return allocator; }

        // Swapchain accessors
        VkSwapchainKHR GetSwapchain() const { return swapchain; }
        VkFormat GetSwapchainFormat() const { return swapchainFormat; }
        VkExtent2D getSwapchainExtent() const { return swapchainExtent; }
        const std::vector<VkImageView>& getSwapchainImageViews() const { return swapchainImageViews; }
        uint32_t getSwapchainImageCount() const { return static_cast<uint32_t>(swapchainImages.size()); }
        /**
         * The frame index, we need it for the many ring buffers in the app.
         * It assumes you are calling Advance at the end of each frame.
         * */
        uint32_t GetFrameIndex()const {return frameIndex % MAX_FRAMES_IN_FLIGHT;}
        /**
         * Advance the frame index.
         * */
        void Advance();
    private:
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;

        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        VkQueue computeQueue = VK_NULL_HANDLE;
        VkQueue transferQueue = VK_NULL_HANDLE;
        QueueFamilyIndices queueFamilies;

        // Swapchain
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent = {0, 0};
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;

        bool createInstance();
        bool pickPhysicalDevice();
        bool createLogicalDevice();
        void createVMA();

        std::vector<const char*> getRequiredExtensions();
        std::vector<const char*> getRequiredLayers();
        std::vector<const char*> getRequiredDeviceExtensions();
        bool checkValidationLayerSupport();
        bool isDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        bool setupDebugMessenger();
        void destroyDebugMessenger();

        // Swapchain helpers
        SwapchainSupportDetails querySwapchainSupport();
        VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
        VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes);
        VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                uint32_t width, uint32_t height);
        void createSwapchainImageViews();
        void destroySwapchain();

        uint32_t frameIndex = 0;
    };
}
#endif //KRAKATOA_VK_CONTEXT_H