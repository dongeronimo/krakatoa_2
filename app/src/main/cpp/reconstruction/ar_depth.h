#ifndef KRAKATOA_AR_DEPTH_H
#define KRAKATOA_AR_DEPTH_H
#include <memory>
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <functional>
#include <vector>
namespace ar{
    class ARSessionManager;
    struct ArDepthIntrinsics;
}
namespace graphics {
    class ArDepthImage;
}
namespace reconstruction {
    class ArDepth;
    typedef std::function<void(ArDepth* arDepth)> OnArDepthCreate;
    /**
     * Holds ar depth data like the intrinsics and the depth buffer.
     * It replaces the variables spread out native lib main loop and hides the Ar Depth Image reducing
     * the clutter in the main loop.
     * */
    //TODO: make this class non-copiable, non-movable
    class ArDepth {
    private:
        static VkDevice gDevice;
        static VmaAllocator  gAllocator;
        static OnArDepthCreate gOnCreate;
    public:
        /**
         * Call this before anything else of this class
         * */
        static void Initialize(VkDevice device, VmaAllocator allocator, OnArDepthCreate OnCreate) {
            gDevice = device;
            gAllocator = allocator;
            gOnCreate = OnCreate;
        }
        /**
         * Get the ArDepth object or null if the ar depth image from the depth api is not
         * available (it takes some time for it to be available)
         * */
        static std::shared_ptr<ArDepth> GetDepth(ar::ARSessionManager& arManager);
        static void Release();

        /**
         * Constructor. It fixes thw dimensions and creates the ArDepthImage object
         * */
        ArDepth(int32_t width, int32_t height, int32_t stride);
        ~ArDepth();
        void UpdateWithMostRecent();
        /**
         * Get the current depth buffer
         * */
        VkBuffer GetCurrentBuffer();
        /**
         * I assume this never changes and have an assert in GetDepth to blow up if this assumption
         * is false.
         * */
        const int32_t Width;
        const int32_t Height;
        const int32_t Stride;

        const ar::ArDepthIntrinsics& GetInstrinsics()const;
    private:
        /**
        * Advances the ring buffer inside ArDepthImage and updates the image
        * */
        void UpdateImage(std::vector<uint16_t>& image, VkExtent2D dimensions);
        std::vector<uint16_t> depthData;

        std::unique_ptr<graphics::ArDepthImage> gArDepthImage;
        std::unique_ptr<ar::ArDepthIntrinsics> arIntrinsics;
        static bool HasVulkanThings();
    };
}


#endif //KRAKATOA_AR_DEPTH_H
