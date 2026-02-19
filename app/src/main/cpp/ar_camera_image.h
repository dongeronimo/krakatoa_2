#ifndef KRAKATOA_AR_CAMERA_IMAGE_H
#define KRAKATOA_AR_CAMERA_IMAGE_H

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "ring_buffer.h"
#include "ar_manager.h"

namespace graphics {

    /**
     * Manages a ring-buffered Vulkan image that receives the ARCore camera feed
     * every frame. The camera provides YUV (NV21/NV12) data which is converted
     * to RGBA on the CPU, written to a persistently-mapped staging buffer, and
     * then copied into the GPU image via inline command buffer commands.
     *
     * No OES texture is ever used. This is a pure CPU-upload path.
     *
     * Usage:
     *   // At start of frame (before BeginFrame / command recording):
     *   cameraImage.AdvanceFrame();
     *
     *   // Inside command buffer recording, after acquiring the camera frame:
     *   cameraImage.Update(cmd, arSessionManager.getCameraFrame());
     *
     *   // Later in the frame, sample the image:
     *   VkImageView view = cameraImage.GetCurrentImageView();
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
         * Convert the camera frame from YUV to RGBA, upload to staging buffer,
         * and record copy + barrier commands into cmd.
         *
         * After this call the current image is in SHADER_READ_ONLY_OPTIMAL layout.
         * If the frame is invalid or the camera has no image yet, this is a no-op.
         *
         * If the camera resolution changed since the last call, all ring-buffer
         * resources are recreated automatically.
         */
        void Update(VkCommandBuffer cmd, const ar::CameraFrame& frame);

        VkImage       GetCurrentImage()     const;
        VkImageView   GetCurrentImageView() const;
        uint32_t      GetWidth()            const { return width; }
        uint32_t      GetHeight()           const { return height; }
        bool          IsValid()             const { return valid; }

    private:
        struct FrameResources {
            VkImage        image            = VK_NULL_HANDLE;
            VmaAllocation  imageAllocation  = VK_NULL_HANDLE;
            VkImageView    imageView        = VK_NULL_HANDLE;
            VkBuffer       stagingBuffer    = VK_NULL_HANDLE;
            VmaAllocation  stagingAllocation= VK_NULL_HANDLE;
            void*          mappedData       = nullptr;   // persistently mapped
        };

        VkDevice     device    = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;

        uint32_t width  = 0;
        uint32_t height = 0;
        bool     valid  = false;

        utils::RingBuffer<FrameResources> frameResources{MAX_FRAMES_IN_FLIGHT};

        void CreateResources(uint32_t w, uint32_t h);
        void DestroyResources();

        /// Convert NV21/NV12 YUV camera data to tightly-packed RGBA.
        static void ConvertYUVToRGBA(const ar::CameraFrame& frame,
                                     uint8_t* dst,
                                     uint32_t dstWidth,
                                     uint32_t dstHeight);
    };

} // namespace graphics

#endif // KRAKATOA_AR_CAMERA_IMAGE_H
