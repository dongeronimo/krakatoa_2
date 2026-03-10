
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
     * I need a ring buffer because the ar depth image will change per frame while being processed
     * by command lists. So the change to the frame N+1 must not touch the image already in use by
     * frame N. That's the same rationale of the mutable mesh.
     *
     * The image comes from ARCore's acquireDepthImage16Bits: always uint16, millimeters, 1 plane.
     * Stored as VK_FORMAT_R16_UINT.
     * */
    class ArDepthImage {
    public:
        ArDepthImage(VkDevice device, VmaAllocator allocator,
                     CommandPoolManager& cmdManager,
                     const std::string& name = "");
        ~ArDepthImage();
        void Advance();
        void UpdateImage(std::vector<uint16_t>& image, VkExtent2D dimensions);
        VkImageView GetCurrentImageView() const { return depthImageViews.Current(); }
        VkExtent2D GetCurrentSize() const { return imageSizes.Current(); }
    private:
        VkDevice device;
        VmaAllocator allocator;
        CommandPoolManager& cmdManager;
        const std::string name;
        utils::RingBuffer<VkImage> depthImages;
        utils::RingBuffer<VmaAllocation> depthImagesAllocations;
        utils::RingBuffer<VkImageView> depthImageViews;
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
