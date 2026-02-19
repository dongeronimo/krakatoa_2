#include "ar_camera_image.h"
#include "vk_debug.h"
#include "android_log.h"
#include "concatenate.h"
#include <cassert>
#include <cstring>

using namespace graphics;

// ============================================================
// Construction / destruction
// ============================================================

ARCameraImage::ARCameraImage(VkDevice device, VmaAllocator allocator)
        : device(device), allocator(allocator) {
    LOGI("ARCameraImage created (no resources yet — waiting for first camera frame)");
}

ARCameraImage::~ARCameraImage() {
    DestroyResources();
    LOGI("ARCameraImage destroyed");
}

// ============================================================
// Ring buffer advancement
// ============================================================

void ARCameraImage::AdvanceFrame() {
    frameResources.Next();
}

// ============================================================
// Per-frame update: memcpy Y+UV planes, GPU upload
// ============================================================

void ARCameraImage::Update(VkCommandBuffer cmd, const ar::CameraFrame& frame) {
    if (!frame.valid || !frame.yPlane || !frame.uvPlane) {
        return;
    }

    uint32_t camW = static_cast<uint32_t>(frame.width);
    uint32_t camH = static_cast<uint32_t>(frame.height);

    // Recreate if the camera resolution changed (or first frame).
    if (camW != width || camH != height) {
        LOGI("ARCameraImage: camera resolution %ux%u (was %ux%u), (re)creating resources",
             camW, camH, width, height);
        DestroyResources();
        CreateResources(camW, camH);
    }

    auto& res = frameResources.Current();

    // ── 1. CPU: memcpy Y plane (row-by-row to strip padding) ──
    {
        auto* dst = static_cast<uint8_t*>(res.yMappedData);
        const uint8_t* src = frame.yPlane;
        if (frame.yRowStride == static_cast<int32_t>(width)) {
            memcpy(dst, src, width * height);
        } else {
            for (uint32_t row = 0; row < height; ++row) {
                memcpy(dst + row * width, src + row * frame.yRowStride, width);
            }
        }
    }

    // ── 2. CPU: memcpy UV plane (interleaved NV12, half res) ──
    {
        const uint32_t uvW = width;      // bytes per row = width (width/2 RG pairs * 2 bytes)
        const uint32_t uvH = height / 2;
        auto* dst = static_cast<uint8_t*>(res.uvMappedData);
        const uint8_t* src = frame.uvPlane;
        if (frame.uvRowStride == static_cast<int32_t>(uvW)) {
            memcpy(dst, src, uvW * uvH);
        } else {
            for (uint32_t row = 0; row < uvH; ++row) {
                memcpy(dst + row * uvW, src + row * frame.uvRowStride, uvW);
            }
        }
    }

    // ── 3. GPU: transition both images UNDEFINED → TRANSFER_DST ──
    VkImageMemoryBarrier toTransferDst[2]{};
    for (int i = 0; i < 2; ++i) {
        toTransferDst[i].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransferDst[i].srcAccessMask       = 0;
        toTransferDst[i].dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransferDst[i].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransferDst[i].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransferDst[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst[i].subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    toTransferDst[0].image = res.yImage;
    toTransferDst[1].image = res.uvImage;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         2, toTransferDst);

    // ── 4. GPU: copy staging buffers → images ──
    VkBufferImageCopy yRegion{};
    yRegion.bufferOffset      = 0;
    yRegion.bufferRowLength   = 0;   // tightly packed
    yRegion.bufferImageHeight = 0;
    yRegion.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    yRegion.imageOffset       = {0, 0, 0};
    yRegion.imageExtent       = {width, height, 1};

    vkCmdCopyBufferToImage(cmd,
                           res.yStagingBuffer, res.yImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &yRegion);

    VkBufferImageCopy uvRegion{};
    uvRegion.bufferOffset      = 0;
    uvRegion.bufferRowLength   = 0;   // tightly packed
    uvRegion.bufferImageHeight = 0;
    uvRegion.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    uvRegion.imageOffset       = {0, 0, 0};
    uvRegion.imageExtent       = {width / 2, height / 2, 1};

    vkCmdCopyBufferToImage(cmd,
                           res.uvStagingBuffer, res.uvImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &uvRegion);

    // ── 5. GPU: transition both images TRANSFER_DST → SHADER_READ_ONLY ──
    VkImageMemoryBarrier toShaderRead[2]{};
    for (int i = 0; i < 2; ++i) {
        toShaderRead[i].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShaderRead[i].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead[i].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        toShaderRead[i].oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShaderRead[i].newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShaderRead[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead[i].subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    toShaderRead[0].image = res.yImage;
    toShaderRead[1].image = res.uvImage;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         2, toShaderRead);

    valid = true;
}

// ============================================================
// Accessors
// ============================================================

VkImageView ARCameraImage::GetCurrentYImageView() const {
    return frameResources.Current().yImageView;
}

VkImageView ARCameraImage::GetCurrentUVImageView() const {
    return frameResources.Current().uvImageView;
}

VkImageView ARCameraImage::GetYImageView(uint32_t index) const {
    return frameResources[index].yImageView;
}

VkImageView ARCameraImage::GetUVImageView(uint32_t index) const {
    return frameResources[index].uvImageView;
}

// ============================================================
// Resource creation / destruction
// ============================================================

/// Helper: create one image + view + staging buffer for a given format and size.
static void CreatePlaneResources(
        VkDevice device, VmaAllocator allocator,
        uint32_t w, uint32_t h, VkFormat format,
        VkImage& outImage, VmaAllocation& outImageAlloc, VkImageView& outView,
        VkBuffer& outStaging, VmaAllocation& outStagingAlloc, void*& outMapped,
        const char* debugName, uint32_t slotIndex)
{
    // ── GPU image (TRANSFER_DST + SAMPLED) ──
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = format;
    imageInfo.extent        = {w, h, 1};
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
                                     &outImage, &outImageAlloc, nullptr);
    assert(result == VK_SUCCESS);
    debug::SetImageName(device, outImage,
                        Concatenate(debugName, "Image[", slotIndex, "]"));

    // ── Image view ──
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    result = vkCreateImageView(device, &viewInfo, nullptr, &outView);
    assert(result == VK_SUCCESS);
    debug::SetImageViewName(device, outView,
                            Concatenate(debugName, "View[", slotIndex, "]"));

    // ── Staging buffer (host-visible, persistently mapped) ──
    // Use VMA_MEMORY_USAGE_AUTO + HOST_ACCESS_SEQUENTIAL_WRITE so VMA
    // picks device-local host-visible memory on unified-memory mobile GPUs.
    uint32_t bytesPerPixel = (format == VK_FORMAT_R8_UNORM) ? 1 : 2;
    VkDeviceSize stagingSize = static_cast<VkDeviceSize>(w) * h * bytesPerPixel;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size  = stagingSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfoOut{};
    result = vmaCreateBuffer(allocator, &bufferInfo, &stagingAllocInfo,
                             &outStaging, &outStagingAlloc, &allocInfoOut);
    assert(result == VK_SUCCESS);
    outMapped = allocInfoOut.pMappedData;
    assert(outMapped != nullptr);
    debug::SetBufferName(device, outStaging,
                         Concatenate(debugName, "Staging[", slotIndex, "]"));
}

void ARCameraImage::CreateResources(uint32_t w, uint32_t h) {
    width  = w;
    height = h;

    const uint32_t uvW = w / 2;
    const uint32_t uvH = h / 2;

    for (uint32_t i = 0; i < frameResources.Size(); ++i) {
        auto& res = frameResources[i];

        CreatePlaneResources(device, allocator, w, h, VK_FORMAT_R8_UNORM,
                             res.yImage, res.yImageAllocation, res.yImageView,
                             res.yStagingBuffer, res.yStagingAllocation, res.yMappedData,
                             "CamY_", i);

        CreatePlaneResources(device, allocator, uvW, uvH, VK_FORMAT_R8G8_UNORM,
                             res.uvImage, res.uvImageAllocation, res.uvImageView,
                             res.uvStagingBuffer, res.uvStagingAllocation, res.uvMappedData,
                             "CamUV_", i);

        LOGI("ARCameraImage: frame resources [%u] created (Y %ux%u R8, UV %ux%u RG8)",
             i, w, h, uvW, uvH);
    }
}

void ARCameraImage::DestroyResources() {
    for (uint32_t i = 0; i < frameResources.Size(); ++i) {
        auto& res = frameResources[i];

        // Y plane
        if (res.yImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, res.yImageView, nullptr);
            res.yImageView = VK_NULL_HANDLE;
        }
        if (res.yImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, res.yImage, res.yImageAllocation);
            res.yImage = VK_NULL_HANDLE;
            res.yImageAllocation = VK_NULL_HANDLE;
        }
        if (res.yStagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, res.yStagingBuffer, res.yStagingAllocation);
            res.yStagingBuffer = VK_NULL_HANDLE;
            res.yStagingAllocation = VK_NULL_HANDLE;
            res.yMappedData = nullptr;
        }

        // UV plane
        if (res.uvImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, res.uvImageView, nullptr);
            res.uvImageView = VK_NULL_HANDLE;
        }
        if (res.uvImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, res.uvImage, res.uvImageAllocation);
            res.uvImage = VK_NULL_HANDLE;
            res.uvImageAllocation = VK_NULL_HANDLE;
        }
        if (res.uvStagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, res.uvStagingBuffer, res.uvStagingAllocation);
            res.uvStagingBuffer = VK_NULL_HANDLE;
            res.uvStagingAllocation = VK_NULL_HANDLE;
            res.uvMappedData = nullptr;
        }
    }

    width  = 0;
    height = 0;
    valid  = false;
}
