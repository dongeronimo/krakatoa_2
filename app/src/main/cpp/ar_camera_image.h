#ifndef KRAKATOA_AR_CAMERA_IMAGE_H
#define KRAKATOA_AR_CAMERA_IMAGE_H

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "ring_buffer.h"
#include "ar_manager.h"

namespace graphics {

    /**
     * Manages ring-buffered Y and UV Vulkan images for the ARCore camera feed.
     *
     * The camera provides NV12/NV21 YUV data.  Y and UV planes are memcpy'd
     * directly into staging buffers (no CPU-side colour conversion) and then
     * copied to GPU-optimal images.  The fragment shader converts YUV -> RGB.
     *
     * Staging buffers use VMA_MEMORY_USAGE_AUTO + HOST_ACCESS_SEQUENTIAL_WRITE
     * so that on mobile unified-memory GPUs VMA places them in device-local,
     * host-visible memory, avoiding an extra DMA copy.
     *
     * Usage:
     *   cameraImage.AdvanceFrame();
     *   cameraImage.Update(cmd, arSessionManager.getCameraFrame());
     *   VkImageView yView  = cameraImage.GetCurrentYImageView();
     *   VkImageView uvView = cameraImage.GetCurrentUVImageView();
     */
    class ARCameraImage {
    public:
        ARCameraImage(VkDevice device, VmaAllocator allocator);
        ~ARCameraImage();

        ARCameraImage(const ARCameraImage&) = delete;
        ARCameraImage& operator=(const ARCameraImage&) = delete;

        /// Advance to the next ring-buffer slot. Call once per frame, before Update.
        void AdvanceFrame();

        /**
         * Memcpy Y and UV planes into staging, record barrier + copy commands.
         * After this call both images are in SHADER_READ_ONLY_OPTIMAL layout.
         */
        void Update(VkCommandBuffer cmd, const ar::CameraFrame& frame);

        VkImageView   GetCurrentYImageView()  const;
        VkImageView   GetCurrentUVImageView() const;
        VkImageView   GetYImageView(uint32_t index)  const;
        VkImageView   GetUVImageView(uint32_t index) const;
        uint32_t      GetWidth()  const { return width; }
        uint32_t      GetHeight() const { return height; }
        bool          IsValid()   const { return valid; }

    private:
        struct FrameResources {
            // Y plane (R8_UNORM, full resolution)
            VkImage        yImage            = VK_NULL_HANDLE;
            VmaAllocation  yImageAllocation  = VK_NULL_HANDLE;
            VkImageView    yImageView        = VK_NULL_HANDLE;
            VkBuffer       yStagingBuffer    = VK_NULL_HANDLE;
            VmaAllocation  yStagingAllocation= VK_NULL_HANDLE;
            void*          yMappedData       = nullptr;

            // UV plane (R8G8_UNORM, half resolution)
            VkImage        uvImage            = VK_NULL_HANDLE;
            VmaAllocation  uvImageAllocation  = VK_NULL_HANDLE;
            VkImageView    uvImageView        = VK_NULL_HANDLE;
            VkBuffer       uvStagingBuffer    = VK_NULL_HANDLE;
            VmaAllocation  uvStagingAllocation= VK_NULL_HANDLE;
            void*          uvMappedData       = nullptr;
        };

        VkDevice     device    = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;

        uint32_t width  = 0;
        uint32_t height = 0;
        bool     valid  = false;

        utils::RingBuffer<FrameResources> frameResources{MAX_FRAMES_IN_FLIGHT};

        void CreateResources(uint32_t w, uint32_t h);
        void DestroyResources();
    };

} // namespace graphics

#endif // KRAKATOA_AR_CAMERA_IMAGE_H
