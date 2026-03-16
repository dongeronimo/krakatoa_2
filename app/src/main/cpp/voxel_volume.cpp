#include "voxel_volume.h"
#include "command_pool_manager.h"
#include "vk_debug.h"
#include "concatenate.h"
#include "android_log.h"
#include <cassert>

using namespace graphics;

VoxelVolume::VoxelVolume(VkDevice device,
                         VmaAllocator allocator,
                         CommandPoolManager& cmdManager,
                         const std::string& name)
    : device(device), allocator(allocator)
{
    // --- Create the 3D image (R8_UINT, device-local) ---
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_3D;
    imgInfo.format        = VK_FORMAT_R8_UINT;
    imgInfo.extent        = { VOLUME_SIZE_X, VOLUME_SIZE_Y, VOLUME_SIZE_Z };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT      // compute read/write
                          | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // for vkCmdClearColorImage
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo gpuAllocInfo{};
    gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkResult result = vmaCreateImage(allocator, &imgInfo, &gpuAllocInfo,
                                     &image, &allocation, nullptr);
    assert(result == VK_SUCCESS);

    // --- Image view (3D) ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image      = image;
    viewInfo.viewType   = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format     = VK_FORMAT_R8_UINT;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;
    result = vkCreateImageView(device, &viewInfo, nullptr, &imageView);
    assert(result == VK_SUCCESS);

    // --- Clear the volume to zero and transition to GENERAL ---
    // Step 1: UNDEFINED → TRANSFER_DST (for the clear)
    // Step 2: Clear
    // Step 3: TRANSFER_DST → GENERAL (for compute storage use)
    cmdManager.SubmitOneShot(CommandPoolManager::QueueType::Graphics,
        [&](VkCommandBuffer cmd) {
            VkImageSubresourceRange range{};
            range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel   = 0;
            range.levelCount     = 1;
            range.baseArrayLayer = 0;
            range.layerCount     = 1;

            // UNDEFINED → TRANSFER_DST
            VkImageMemoryBarrier toTransferDst{};
            toTransferDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransferDst.srcAccessMask        = 0;
            toTransferDst.dstAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransferDst.oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransferDst.newLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransferDst.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            toTransferDst.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            toTransferDst.image                = image;
            toTransferDst.subresourceRange     = range;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr,
                                 1, &toTransferDst);

            // Clear to zero
            VkClearColorValue clearValue{};
            clearValue.uint32[0] = 0;
            clearValue.uint32[1] = 0;
            clearValue.uint32[2] = 0;
            clearValue.uint32[3] = 0;
            vkCmdClearColorImage(cmd, image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &clearValue, 1, &range);

            // TRANSFER_DST → GENERAL (ready for compute storage)
            VkImageMemoryBarrier toGeneral{};
            toGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toGeneral.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
            toGeneral.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            toGeneral.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toGeneral.newLayout            = VK_IMAGE_LAYOUT_GENERAL;
            toGeneral.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            toGeneral.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            toGeneral.image                = image;
            toGeneral.subresourceRange     = range;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr,
                                 1, &toGeneral);
        });

    // Debug names
    debug::SetImageName(device, image, Concatenate(name, ":Image"));
    debug::SetImageViewName(device, imageView, Concatenate(name, ":ImageView"));

    LOGI("VoxelVolume created: %ux%ux%u R8_UINT (~%u MB)",
         VOLUME_SIZE_X, VOLUME_SIZE_Y, VOLUME_SIZE_Z,
         (VOLUME_SIZE_X * VOLUME_SIZE_Y * VOLUME_SIZE_Z) / (1024 * 1024));
}

VoxelVolume::~VoxelVolume() {
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    if (image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, image, allocation);
    }
    LOGI("VoxelVolume destroyed");
}
