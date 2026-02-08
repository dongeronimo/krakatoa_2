#ifndef KRAKATOA_VK_CONTEXT_H
#define KRAKATOA_VK_CONTEXT_H
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>

namespace graphics {
    class VkContext {
    public:
        VkContext();
        ~VkContext();
        bool Initialize();
        VkInstance GetInstance() const {return instance;}
    private:
        VkInstance instance = VK_NULL_HANDLE;
        bool createInstance();
        std::vector<const char*> getRequiredExtensions();
        std::vector<const char*> getRequiredLayers();
        bool checkValidationLayerSupport();

    };
}


#endif //KRAKATOA_VK_CONTEXT_H
