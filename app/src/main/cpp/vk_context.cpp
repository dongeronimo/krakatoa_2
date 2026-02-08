
#include "vk_context.h"
#include "android_log.h"
#include <cassert>
using namespace graphics;
void fillApplicationInfo(VkApplicationInfo& info);
void checkVulkan1_3();

VkContext::VkContext() {

}

VkContext::~VkContext() {
    vkDestroyInstance(instance, nullptr);
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
