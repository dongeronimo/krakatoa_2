#include "ar_depth_image.h"
#include "command_pool_manager.h"
#include "vk_debug.h"
#include "concatenate.h"
#include <vulkan/vulkan.h>
#include <cassert>
#include <cstring>
using namespace graphics;

ArDepthImage::ArDepthImage(VkDevice device, VmaAllocator allocator,
                           CommandPoolManager &cmdManager,
                           const std::string &name) :
                           device(device), allocator(allocator),
                           cmdManager(cmdManager), name(name) {
    for(auto i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
        depthImages[i] = VK_NULL_HANDLE;
        depthImagesAllocations[i] = VK_NULL_HANDLE;
        depthImageViews[i] = VK_NULL_HANDLE;
        imageSizes[i] = {0,0};
        slotGeneration[i] = 0;
    }
}

ArDepthImage::~ArDepthImage() {
    for(auto i=0; i<MAX_FRAMES_IN_FLIGHT;i++) {
        if(depthImages[i] != VK_NULL_HANDLE){
            vkDestroyImageView(device, depthImageViews[i], nullptr);
            vmaDestroyImage(allocator, depthImages[i], depthImagesAllocations[i]);
        }
    }
}

void ArDepthImage::Advance() {
    depthImages.Next();
    depthImagesAllocations.Next();
    depthImageViews.Next();
    imageSizes.Next();
    slotGeneration.Next();
    if(slotGeneration.Current() < pendingGeneration) {
        UpdateCurrentSlotIfPending();
        slotGeneration.Current() = pendingGeneration;
    }
}

void ArDepthImage::UpdateImage(std::vector<uint16_t>& image, VkExtent2D dimensions) {
    bool same = pendingImage == image;
    if(same) return;
    pendingImage.assign(image.begin(), image.end());
    pendingDimension = dimensions;
    pendingGeneration++;
}

void ArDepthImage::UpdateCurrentSlotIfPending() {
    if(depthImages.Current() != VK_NULL_HANDLE){
        vkDestroyImageView(device, depthImageViews.Current(), nullptr);
        vmaDestroyImage(allocator, depthImages.Current(), depthImagesAllocations.Current());
    }
    UploadToCurrentSlot();
    SetObjectsNames();
}

void ArDepthImage::UploadToCurrentSlot() {
    // 1. Create the GPU image (R16_UINT, optimal tiling, transfer dst + sampled)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R16_UINT;
    imageInfo.extent        = {pendingDimension.width, pendingDimension.height, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkResult result = vmaCreateImage(allocator, &imageInfo, &imageAllocInfo,
                                     &depthImages.Current(),
                                     &depthImagesAllocations.Current(),
                                     nullptr);
    assert(result == VK_SUCCESS);

    // 2. Create staging buffer (host-visible, persistently mapped)
    VkDeviceSize stagingSize = static_cast<VkDeviceSize>(pendingImage.size()) * sizeof(uint16_t);

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = stagingSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo mapInfo{};
    result = vmaCreateBuffer(allocator, &bufInfo, &stagingAllocInfo,
                             &stagingBuffer, &stagingAllocation, &mapInfo);
    assert(result == VK_SUCCESS);

    // 3. Copy pending data into staging buffer
    memcpy(mapInfo.pMappedData, pendingImage.data(), stagingSize);

    // 4. Upload staging → image via CommandPoolManager (handles layout transitions)
    cmdManager.UploadImage(stagingBuffer, depthImages.Current(),
                           pendingDimension.width, pendingDimension.height,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // 5. Destroy staging buffer (upload is synchronous/blocking)
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    // 6. Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = depthImages.Current();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = VK_FORMAT_R16_UINT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    result = vkCreateImageView(device, &viewInfo, nullptr, &depthImageViews.Current());
    assert(result == VK_SUCCESS);

    // 7. Update size for this slot
    imageSizes.Current() = pendingDimension;
}

void ArDepthImage::SetObjectsNames() {
    debug::SetImageName(device, depthImages.Current(),
                        Concatenate(name, " Depth Image Generation #", pendingGeneration));
    debug::SetImageViewName(device, depthImageViews.Current(),
                            Concatenate(name, " Depth ImageView Generation #", pendingGeneration));
}
