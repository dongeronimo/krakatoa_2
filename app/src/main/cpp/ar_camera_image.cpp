#include "ar_camera_image.h"
#include "vk_debug.h"
#include "android_log.h"
#include "concatenate.h"
#include <cassert>
#include <algorithm>
#include <cstring>

using namespace graphics;

// ============================================================
// YUV → RGBA conversion (BT.601 full-range)
// ============================================================

static inline uint8_t Clamp(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

void ARCameraImage::ConvertYUVToRGBA(const ar::CameraFrame& frame,
                                      uint8_t* dst,
                                      uint32_t dstWidth,
                                      uint32_t dstHeight) {
    const uint8_t* yPlane  = frame.yPlane;
    const uint8_t* uvPlane = frame.uvPlane;

    const int32_t yRowStride   = frame.yRowStride;
    const int32_t uvRowStride  = frame.uvRowStride;
    const int32_t uvPixelStride = frame.uvPixelStride;

    // UV plane is half-resolution in both dimensions.
    // For each 2x2 block of Y pixels there is one (U,V) pair.
    //
    // ARCore plane 1 (U) with pixelStride==2 means interleaved UV (NV12):
    //   uvPlane[col * 2 + 0] = U
    //   uvPlane[col * 2 + 1] = V
    //
    // With pixelStride==1 we'd have separate U and V planes,
    // but ARCore on Android virtually always gives pixelStride==2.

    for (uint32_t row = 0; row < dstHeight; ++row) {
        const uint8_t* yRow  = yPlane  + row * yRowStride;
        const uint8_t* uvRow = uvPlane + (row / 2) * uvRowStride;
        uint8_t* out = dst + row * dstWidth * 4;

        for (uint32_t col = 0; col < dstWidth; ++col) {
            int Y = yRow[col];
            int uvIndex = (col / 2) * uvPixelStride;
            int U = uvRow[uvIndex]     - 128;
            int V = uvRow[uvIndex + 1] - 128;

            // BT.601 YUV → RGB
            int R = Y + (int)(1.402f  * V);
            int G = Y - (int)(0.344f  * U) - (int)(0.714f * V);
            int B = Y + (int)(1.772f  * U);

            out[col * 4 + 0] = Clamp(R);
            out[col * 4 + 1] = Clamp(G);
            out[col * 4 + 2] = Clamp(B);
            out[col * 4 + 3] = 255;
        }
    }
}

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
// Per-frame update: YUV→RGBA, staging copy, GPU upload
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

    // ── 1. CPU: convert YUV → RGBA into the staging buffer ──
    ConvertYUVToRGBA(frame,
                     static_cast<uint8_t*>(res.mappedData),
                     width, height);

    // ── 2. GPU: transition image UNDEFINED → TRANSFER_DST ──
    VkImageMemoryBarrier toTransferDst{};
    toTransferDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferDst.srcAccessMask       = 0;
    toTransferDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransferDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.image               = res.image;
    toTransferDst.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toTransferDst);

    // ── 3. GPU: copy staging buffer → image ──
    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;   // tightly packed
    region.bufferImageHeight = 0;   // tightly packed
    region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset       = {0, 0, 0};
    region.imageExtent       = {width, height, 1};

    vkCmdCopyBufferToImage(cmd,
                           res.stagingBuffer,
                           res.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    // ── 4. GPU: transition image TRANSFER_DST → SHADER_READ_ONLY ──
    VkImageMemoryBarrier toShaderRead{};
    toShaderRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    toShaderRead.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image               = res.image;
    toShaderRead.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toShaderRead);

    valid = true;
}

// ============================================================
// Accessors
// ============================================================

VkImage ARCameraImage::GetCurrentImage() const {
    return frameResources.Current().image;
}

VkImageView ARCameraImage::GetCurrentImageView() const {
    return frameResources.Current().imageView;
}

// ============================================================
// Resource creation / destruction
// ============================================================

void ARCameraImage::CreateResources(uint32_t w, uint32_t h) {
    width  = w;
    height = h;

    const VkDeviceSize stagingSize = static_cast<VkDeviceSize>(w) * h * 4; // RGBA

    for (uint32_t i = 0; i < frameResources.Size(); ++i) {
        auto& res = frameResources[i];

        // ── GPU image (TRANSFER_DST + SAMPLED) ──
        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
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
                                         &res.image, &res.imageAllocation, nullptr);
        assert(result == VK_SUCCESS);
        debug::SetImageName(device, res.image,
                            Concatenate("ARCameraImage[", i, "]"));

        // ── Image view ──
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = res.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        result = vkCreateImageView(device, &viewInfo, nullptr, &res.imageView);
        assert(result == VK_SUCCESS);
        debug::SetImageViewName(device, res.imageView,
                                Concatenate("ARCameraImageView[", i, "]"));

        // ── Staging buffer (host-visible, persistently mapped) ──
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size  = stagingSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocInfoOut{};
        result = vmaCreateBuffer(allocator, &bufferInfo, &stagingAllocInfo,
                                 &res.stagingBuffer, &res.stagingAllocation,
                                 &allocInfoOut);
        assert(result == VK_SUCCESS);
        res.mappedData = allocInfoOut.pMappedData;
        assert(res.mappedData != nullptr);
        debug::SetBufferName(device, res.stagingBuffer,
                             Concatenate("ARCameraStagingBuffer[", i, "]"));

        LOGI("ARCameraImage: frame resources [%u] created (%ux%u, staging %llu bytes)",
             i, w, h, (unsigned long long)stagingSize);
    }
}

void ARCameraImage::DestroyResources() {
    for (uint32_t i = 0; i < frameResources.Size(); ++i) {
        auto& res = frameResources[i];

        if (res.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, res.imageView, nullptr);
            res.imageView = VK_NULL_HANDLE;
        }
        if (res.image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, res.image, res.imageAllocation);
            res.image           = VK_NULL_HANDLE;
            res.imageAllocation = VK_NULL_HANDLE;
        }
        if (res.stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, res.stagingBuffer, res.stagingAllocation);
            res.stagingBuffer    = VK_NULL_HANDLE;
            res.stagingAllocation= VK_NULL_HANDLE;
            res.mappedData       = nullptr;
        }
    }

    width  = 0;
    height = 0;
    valid  = false;
}
