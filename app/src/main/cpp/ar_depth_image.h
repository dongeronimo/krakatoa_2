
#ifndef KRAKATOA_AR_DEPTH_IMAGE_H
#define KRAKATOA_AR_DEPTH_IMAGE_H
#include <vulkan/vulkan.h>
#include "ring_buffer.h"
#include "vk_mem_alloc.h"
#include <vector>
#include <string>
namespace graphics {
    class CommandPoolManager;
    /**
     * Ring-buffered depth data from ARCore's acquireDepthImage16Bits.
     * Always uint16, millimeters, 1 plane, stored as R16_UINT.
     *
     * Uses a host-mapped VkBuffer (no staging buffer) since Android has
     * unified memory. Follows the same pattern as MutableMesh.
     * */
    class ArDepthImage {
    public:
        ArDepthImage(VkDevice device, VmaAllocator allocator,
                     const std::string& name = "");
        ~ArDepthImage();
        void Advance();
        void UpdateImage(std::vector<uint16_t>& image, VkExtent2D dimensions);
        VkBuffer GetCurrentBuffer() const { return depthBuffers.Current(); }
        VkExtent2D GetCurrentSize() const { return imageSizes.Current(); }
        uint32_t GetCurrentPixelCount() const {
            auto sz = imageSizes.Current();
            return sz.width * sz.height;
        }
    private:
        VkDevice device;
        VmaAllocator allocator;
        const std::string name;
        utils::RingBuffer<VkBuffer> depthBuffers;
        utils::RingBuffer<VmaAllocation> depthBufferAllocations;
        utils::RingBuffer<VkExtent2D> imageSizes;
        utils::RingBuffer<uint64_t> slotGeneration;
        std::vector<uint16_t> pendingImage;
        VkExtent2D pendingDimension;
        uint64_t pendingGeneration = 0;
        void UpdateCurrentSlotIfPending();
        void UploadToCurrentSlot();
        void SetObjectsNames();
    };
}


#endif //KRAKATOA_AR_DEPTH_IMAGE_H
