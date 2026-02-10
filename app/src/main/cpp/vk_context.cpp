
#include "vk_context.h"
#include "android_log.h"
#include <cassert>
#include <set>
#include <map>
using namespace graphics;
void fillApplicationInfo(VkApplicationInfo& info);
void checkVulkan1_3();

VkContext::VkContext() {

}

VkContext::~VkContext() {
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

bool VkContext::createInstance() {
    checkVulkan1_3();
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
    result = pickPhysicalDevice();
    assert(result);
    result = createLogicalDevice();
    assert(result);
    LOGI("Vulkan Context initialized successfully!");
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
void checkVulkan1_3() {
    uint32_t apiVersion;
    vkEnumerateInstanceVersion(&apiVersion);

    uint32_t major = VK_API_VERSION_MAJOR(apiVersion);
    uint32_t minor = VK_API_VERSION_MINOR(apiVersion);
    uint32_t patch = VK_API_VERSION_PATCH(apiVersion);

    LOGI("Vulkan API Version: %d.%d.%d", major, minor, patch);
    if (apiVersion < VK_API_VERSION_1_3) {
        LOGE("Vulkan 1.3 not supported! Available: %d.%d.%d", major, minor, patch);
        assert(false);
    }
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
