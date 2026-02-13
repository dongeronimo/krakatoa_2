#include "vk_context.h"
#include "vk_debug.h"
#include "android_log.h"
#include <cassert>
#include <set>
#include <map>
using namespace graphics;
void fillApplicationInfo(VkApplicationInfo& info);
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData) {

    const char* typeStr = "GENERAL";
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        typeStr = "VALIDATION";
    } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        typeStr = "PERFORMANCE";
    }

    bool isError = (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    bool isAdrenoWarning = strstr(pCallbackData->pMessage, "VKDBGUTILWARN") != nullptr;
    if (isAdrenoWarning) {
        return VK_FALSE; // silencia completamente
    }
    if (isError) {
        LOGE("[VULKAN ERROR/%s] %s", typeStr, pCallbackData->pMessage);

        if (pCallbackData->objectCount > 0) {
            LOGE("  Objects involved: %u", pCallbackData->objectCount);
            for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
                LOGE("    [%u] Type=%d Handle=%p Name=%s",
                     i,
                     pCallbackData->pObjects[i].objectType,
                     (void*)pCallbackData->pObjects[i].objectHandle,
                     pCallbackData->pObjects[i].pObjectName ?
                     pCallbackData->pObjects[i].pObjectName : "unnamed");
            }
        }

        // Adreno driver warnings come as errors with VKDBGUTILWARN prefix — skip them
        if (!isAdrenoWarning) {
            assert(false && "Vulkan validation error occurred!");
        } else {
            LOGW("Adreno driver warning (non-fatal), continuing");
        }
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOGW("[VULKAN WARNING/%s] %s", typeStr, pCallbackData->pMessage);
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        LOGI("[VULKAN INFO/%s] %s", typeStr, pCallbackData->pMessage);
    } else {
        LOGD("[VULKAN VERBOSE/%s] %s", typeStr, pCallbackData->pMessage);
    }

    return VK_FALSE;
}

VkContext::VkContext() {

}

VkContext::~VkContext() {
    destroySwapchain();
    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
    }
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    destroyDebugMessenger();
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

bool VkContext::createInstance() {
    VkApplicationInfo appInfo;
    fillApplicationInfo(appInfo);
    auto extensions = getRequiredExtensions();
    LOGI("Required extensions:");
    for (const auto* ext : extensions) {
        LOGI("  - %s", ext);
    }
    auto layers = getRequiredLayers();
    if (!checkValidationLayerSupport()) {//TODO: Must not be used when in release;
        LOGE("Validation layers requested but not available");
        assert(false);
    }
    // Instance create info
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance: %d", result);
        return false;
    }
    return true;
}

std::vector<const char *> VkContext::getRequiredExtensions() {
    std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return extensions;
}

std::vector<const char *> VkContext::getRequiredLayers() {
    std::vector<const char*> layers;
    layers.push_back("VK_LAYER_KHRONOS_validation");
    return layers;
}

bool VkContext::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    LOGI("Total available layers: %u", layerCount);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    LOGI("Available validation layers:");
    for (const auto& layerProperties : availableLayers) {
        LOGI("  - %s", layerProperties.layerName);
    }

    auto requiredLayers = getRequiredLayers();

    for (const char* layerName : requiredLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            LOGE("Validation layer not found: %s", layerName);
            return false;
        }
    }

    return true;
}

bool VkContext::Initialize() {
    bool result = createInstance();
    assert(result);
    LOGI("Vulkan instance created successfully");
    debug::Initialize(instance);
    result = setupDebugMessenger();
    if (!result) {
        LOGI("Debug messenger setup failed, continuing without it");
    }
    result = pickPhysicalDevice();
    assert(result);
    result = createLogicalDevice();
    assert(result);
    LOGI("Vulkan Context initialized successfully!");
    createVMA();
    LOGI("VMA created.");
    return true;
}

void fillApplicationInfo(VkApplicationInfo& info) {
    info = {};
    info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    info.pApplicationName = "Krakatoa";
    info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    info.pEngineName = "Krakatoa Engine";
    info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    info.apiVersion = VK_API_VERSION_1_3;  // IMPORTANTE: Vulkan 1.3!
}


std::vector<const char*> VkContext::getRequiredDeviceExtensions() {
    return {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
}

bool VkContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        LOGE("Failed to find GPUs with Vulkan support");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    LOGI("Found %u physical device(s)", deviceCount);

    for (const auto& dev : devices) {
        if (isDeviceSuitable(dev)) {
            physicalDevice = dev;
            queueFamilies = findQueueFamilies(dev);

            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(dev, &deviceProperties);

            LOGI("Selected GPU: %s", deviceProperties.deviceName);
            LOGI("  API Version: %d.%d.%d",
                 VK_API_VERSION_MAJOR(deviceProperties.apiVersion),
                 VK_API_VERSION_MINOR(deviceProperties.apiVersion),
                 VK_API_VERSION_PATCH(deviceProperties.apiVersion));

            return true;
        }
    }

    LOGE("Failed to find suitable GPU");
    return false;
}

bool VkContext::isDeviceSuitable(VkPhysicalDevice dev) {
    QueueFamilyIndices indices = findQueueFamilies(dev);
    bool extensionsSupported = checkDeviceExtensionSupport(dev);

    return indices.isComplete() && extensionsSupported;
}

QueueFamilyIndices VkContext::findQueueFamilies(VkPhysicalDevice dev) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, families.data());

    LOGI("Available queue families: %u", queueFamilyCount);

    int dedicatedComputeFamily = -1;
    int dedicatedTransferFamily = -1;

    for (uint32_t i = 0; i < families.size(); i++) {
        const auto& family = families[i];

        LOGI("Queue Family %u: Count=%u Graphics=%d Compute=%d Transfer=%d", i,
             family.queueCount,
             !!(family.queueFlags & VK_QUEUE_GRAPHICS_BIT),
             !!(family.queueFlags & VK_QUEUE_COMPUTE_BIT),
             !!(family.queueFlags & VK_QUEUE_TRANSFER_BIT));

        // Graphics (primeira com graphics)
        if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) && !indices.graphicsFamily.has_value()) {
            indices.graphicsFamily = i;
            indices.presentFamily = i;  // Android: graphics = present
        }

        // Compute dedicada (sem graphics)
        if ((family.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(family.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            dedicatedComputeFamily == -1) {
            dedicatedComputeFamily = i;
        }

        // Transfer dedicada (sem graphics/compute)
        if ((family.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(family.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(family.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            dedicatedTransferFamily == -1) {
            dedicatedTransferFamily = i;
        }
    }

    // Compute: dedicada > compartilhada com graphics
    if (dedicatedComputeFamily != -1) {
        indices.computeFamily = dedicatedComputeFamily;
        LOGI("Using dedicated compute queue family %d", dedicatedComputeFamily);
    } else if (indices.graphicsFamily.has_value()) {
        indices.computeFamily = indices.graphicsFamily.value();
        LOGI("Using graphics queue family for compute");
    }

    // Transfer: dedicada > compartilhada
    if (dedicatedTransferFamily != -1) {
        indices.transferFamily = dedicatedTransferFamily;
        LOGI("Using dedicated transfer queue family %d", dedicatedTransferFamily);
    } else if (indices.computeFamily.has_value()) {
        indices.transferFamily = indices.computeFamily.value();
        LOGI("Using compute queue family for transfer");
    } else if (indices.graphicsFamily.has_value()) {
        indices.transferFamily = indices.graphicsFamily.value();
        LOGI("Using graphics queue family for transfer");
    }

    return indices;
}

bool VkContext::checkDeviceExtensionSupport(VkPhysicalDevice dev) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, availableExtensions.data());

    LOGI("Available device extensions (%u):", extensionCount);
    for (const auto& ext : availableExtensions) {
        LOGI("  - %s", ext.extensionName);
    }

    auto requiredExtensions = getRequiredDeviceExtensions();

    for (const char* required : requiredExtensions) {
        bool found = false;
        for (const auto& available : availableExtensions) {
            if (strcmp(required, available.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOGE("Missing required device extension: %s", required);
            return false;
        }
    }

    return true;
}

bool VkContext::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    // Get family properties
    uint32_t familyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    // Planejar alocacao de queues por familia
    struct FamilyAllocation {
        uint32_t familyIndex;
        uint32_t queueCount;
        std::vector<const char*> uses;
    };

    std::map<uint32_t, FamilyAllocation> allocations;

    // Adicionar usos
    auto addUse = [&](uint32_t family, const char* use) {
        if (allocations.find(family) == allocations.end()) {
            allocations[family] = {family, 0, {}};
        }
        allocations[family].uses.push_back(use);
        allocations[family].queueCount++;
    };

    addUse(indices.graphicsFamily.value(), "Graphics");
    addUse(indices.presentFamily.value(), "Present");
    addUse(indices.computeFamily.value(), "Compute");
    addUse(indices.transferFamily.value(), "Transfer");

    // Calcular indices das queues
    std::map<uint32_t, uint32_t> nextQueueIndex;

    auto allocateQueueIndex = [&](uint32_t family) -> uint32_t {
        uint32_t idx = nextQueueIndex[family]++;
        uint32_t maxQueues = families[family].queueCount;

        if (nextQueueIndex[family] > maxQueues) {
            LOGE("Family %u exhausted, reusing last queue", family);
            return maxQueues - 1;
        }
        return idx;
    };

    indices.graphicsQueueIndex = allocateQueueIndex(indices.graphicsFamily.value());
    indices.presentQueueIndex = allocateQueueIndex(indices.presentFamily.value());
    indices.computeQueueIndex = allocateQueueIndex(indices.computeFamily.value());
    indices.transferQueueIndex = allocateQueueIndex(indices.transferFamily.value());

    // Criar queue create infos
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::vector<std::vector<float>> priorities;

    for (auto& [family, alloc] : allocations) {
        uint32_t actualCount = std::min(alloc.queueCount, families[family].queueCount);

        LOGI("Family %u: Creating %u queue(s) for %zu use(s)",
             family, actualCount, alloc.uses.size());
        for (const char* use : alloc.uses) {
            LOGI("  - %s", use);
        }

        std::vector<float> prios(actualCount, 1.0f);
        priorities.push_back(prios);

        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = actualCount;
        queueInfo.pQueuePriorities = priorities.back().data();

        queueCreateInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    auto extensions = getRequiredDeviceExtensions();

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create logical device: %d", result);
        return false;
    }

    // Get queues
    vkGetDeviceQueue(device, indices.graphicsFamily.value(),
                     indices.graphicsQueueIndex, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(),
                     indices.presentQueueIndex, &presentQueue);
    vkGetDeviceQueue(device, indices.computeFamily.value(),
                     indices.computeQueueIndex, &computeQueue);
    vkGetDeviceQueue(device, indices.transferFamily.value(),
                     indices.transferQueueIndex, &transferQueue);

    LOGI("========================================");
    LOGI("Logical device created (Vulkan 1.0)");
    LOGI("  Graphics : Family %u[%u] -> %p",
         indices.graphicsFamily.value(), indices.graphicsQueueIndex, graphicsQueue);
    LOGI("  Present  : Family %u[%u] -> %p",
         indices.presentFamily.value(), indices.presentQueueIndex, presentQueue);
    LOGI("  Compute  : Family %u[%u] -> %p",
         indices.computeFamily.value(), indices.computeQueueIndex, computeQueue);
    LOGI("  Transfer : Family %u[%u] -> %p",
         indices.transferFamily.value(), indices.transferQueueIndex, transferQueue);

    bool asyncCompute = (computeQueue != graphicsQueue);
    bool asyncTransfer = (transferQueue != graphicsQueue &&
                          transferQueue != computeQueue);

    LOGI("Capabilities:");
    LOGI("  Async Compute : %s", asyncCompute ? "YES" : "NO");
    LOGI("  Async Transfer: %s", asyncTransfer ? "YES" : "NO");
    LOGI("========================================");

    queueFamilies = indices;
    return true;
}
bool VkContext::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    // Severidades que queremos capturar
    createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    // Tipos de mensagem
    createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr;  // Opcional: passar dados customizados

    // Carregar function pointer (nao esta no core Vulkan 1.0)
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

    if (func == nullptr) {
        LOGE("vkCreateDebugUtilsMessengerEXT not available");
        return false;
    }

    VkResult result = func(instance, &createInfo, nullptr, &debugMessenger);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create debug messenger: %d", result);
        return false;
    }

    LOGI("Debug messenger created successfully");
    return true;
}

void VkContext::destroyDebugMessenger() {
    if (debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

        if (func != nullptr) {
            func(instance, debugMessenger, nullptr);
        }

        debugMessenger = VK_NULL_HANDLE;
    }
}

void VkContext::createVMA() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0; // já que está preso no 1.0
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;

    VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator);
    assert(result == VK_SUCCESS);
}

bool VkContext::CreateSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.window = window;

    VkResult result = vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, &surface);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Android surface: %d", result);
        return false;
    }

    LOGI("Vulkan surface created");
    return true;
}

SwapchainSupportDetails VkContext::querySwapchainSupport() {
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (formatCount > 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                             details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount,
                                                  details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VkContext::chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) {
    // Prefer SRGB with 8-bit per channel
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    // Fallback: UNORM
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    // Last resort: whatever is available
    LOGW("Preferred surface format not found, using first available");
    return formats[0];
}

VkPresentModeKHR VkContext::choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) {
    // FIFO is guaranteed by the spec, acts as vsync
    // Mailbox is preferred for low latency (triple buffering)
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            LOGI("Present mode: MAILBOX (triple buffering)");
            return mode;
        }
    }

    LOGI("Present mode: FIFO (vsync)");
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VkContext::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                   uint32_t width, uint32_t height) {
    // If currentExtent is not the special value 0xFFFFFFFF, the surface size is fixed
    if (capabilities.currentExtent.width != UINT32_MAX) {
        LOGI("Using surface's current extent: %ux%u",
             capabilities.currentExtent.width, capabilities.currentExtent.height);
        return capabilities.currentExtent;
    }

    // Otherwise, clamp our desired size to the surface limits
    VkExtent2D extent = {width, height};
    extent.width = std::max(capabilities.minImageExtent.width,
                            std::min(capabilities.maxImageExtent.width, extent.width));
    extent.height = std::max(capabilities.minImageExtent.height,
                             std::min(capabilities.maxImageExtent.height, extent.height));

    LOGI("Chosen swapchain extent: %ux%u", extent.width, extent.height);
    return extent;
}

bool VkContext::CreateSwapchain(uint32_t width, uint32_t height) {
    assert(surface != VK_NULL_HANDLE);
    assert(device != VK_NULL_HANDLE);

    SwapchainSupportDetails support = querySwapchainSupport();

    if (support.formats.empty() || support.presentModes.empty()) {
        LOGE("Swapchain not supported: no formats or present modes");
        return false;
    }

    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    VkExtent2D extent = chooseExtent(support.capabilities, width, height);

    // Request one more image than the minimum for triple buffering
    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    LOGI("Swapchain config:");
    LOGI("  Format: %d, ColorSpace: %d", surfaceFormat.format, surfaceFormat.colorSpace);
    LOGI("  Present mode: %d", presentMode);
    LOGI("  Extent: %ux%u", extent.width, extent.height);
    LOGI("  Image count: %u (min=%u, max=%u)",
         imageCount, support.capabilities.minImageCount, support.capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // On Android, graphics and present are always the same family
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create swapchain: %d", result);
        return false;
    }

    // Retrieve swapchain images
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    swapchainFormat = surfaceFormat.format;
    swapchainExtent = extent;

    LOGI("Swapchain created with %u images", imageCount);

    for (uint32_t i = 0; i < imageCount; i++) {
        debug::SetImageName(device, swapchainImages[i],
                             Concatenate("SwapchainImage[", i, "]"));
    }

    createSwapchainImageViews();
    return true;
}

void VkContext::createSwapchainImageViews() {
    swapchainImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(device, &viewInfo, nullptr,
                                            &swapchainImageViews[i]);
        assert(result == VK_SUCCESS);
    }

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        debug::SetImageViewName(device, swapchainImageViews[i],
                             Concatenate("SwapchainImageView[", i, "]"));
    }

    LOGI("Created %zu swapchain image views", swapchainImageViews.size());
}

bool VkContext::RecreateSwapchain(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(device);

    VkSwapchainKHR oldSwapchain = swapchain;
    swapchain = VK_NULL_HANDLE;

    // Destroy image views (images are owned by the old swapchain)
    for (auto iv : swapchainImageViews) {
        vkDestroyImageView(device, iv, nullptr);
    }
    swapchainImageViews.clear();
    swapchainImages.clear();

    // Query fresh support details
    SwapchainSupportDetails support = querySwapchainSupport();
    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    VkExtent2D extent = chooseExtent(support.capabilities, width, height);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;  // pass old for driver recycling

    VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);

    // Destroy old swapchain after creating new one
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
    }

    if (result != VK_SUCCESS) {
        LOGE("Failed to recreate swapchain: %d", result);
        return false;
    }

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    swapchainFormat = surfaceFormat.format;
    swapchainExtent = extent;

    for (uint32_t i = 0; i < imageCount; i++) {
        debug::SetImageName(device, swapchainImages[i],
                             Concatenate("SwapchainImage[", i, "]"));
    }

    createSwapchainImageViews();

    LOGI("Swapchain recreated: %ux%u, %u images", extent.width, extent.height, imageCount);
    return true;
}

void VkContext::destroySwapchain() {
    for (auto iv : swapchainImageViews) {
        if (iv != VK_NULL_HANDLE) {
            vkDestroyImageView(device, iv, nullptr);
        }
    }
    swapchainImageViews.clear();
    swapchainImages.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}