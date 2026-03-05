#include "texture2d.h"
#include "command_pool_manager.h"
#include "vk_debug.h"
#include "android_log.h"
#include "concatenate.h"
#include <cassert>
#include <cstring>
using namespace graphics;

Texture2D::Texture2D(VkDevice device,
                     VmaAllocator allocator,
                     CommandPoolManager& cmdManager,
                     const std::vector<uint8_t>& pixels,
                     uint32_t width,
                     uint32_t height,
                     VkFormat format,
                     const std::string& name)
        : device(device), allocator(allocator), format(format), width(width), height(height) {

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(pixels.size());
    assert(imageSize > 0);
    assert(width > 0 && height > 0);

    // --- Staging buffer (host-visible) ---
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = imageSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VkResult result = vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                                      &stagingBuffer, &stagingAlloc, nullptr);
    assert(result == VK_SUCCESS);

    // Copy pixel data to staging
    void* mapped;
    vmaMapMemory(allocator, stagingAlloc, &mapped);
    memcpy(mapped, pixels.data(), imageSize);
    vmaUnmapMemory(allocator, stagingAlloc);

    // --- GPU image (device-local, optimal tiling) ---
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = format;
    imgInfo.extent        = {width, height, 1};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo gpuAllocInfo{};
    gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    result = vmaCreateImage(allocator, &imgInfo, &gpuAllocInfo,
                            &image, &allocation, nullptr);
    assert(result == VK_SUCCESS);

    // --- Upload via transfer queue with layout transition ---
    cmdManager.UploadImage(stagingBuffer, image, width, height,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Destroy staging
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // --- Image view ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image      = image;
    viewInfo.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format     = format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;
    result = vkCreateImageView(device, &viewInfo, nullptr, &imageView);
    assert(result == VK_SUCCESS);

    // Debug names
    if (!name.empty()) {
        debug::SetImageName(device, image, Concatenate(name, ":Image"));
        debug::SetImageViewName(device, imageView, Concatenate(name, ":ImageView"));
    }

    LOGI("Texture2D created: %ux%u format=%d (%zu bytes) name='%s'",
         width, height, format, (size_t)imageSize, name.c_str());
}

Texture2D::~Texture2D() {
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    if (image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, image, allocation);
    }
    LOGI("Texture2D destroyed");
}
