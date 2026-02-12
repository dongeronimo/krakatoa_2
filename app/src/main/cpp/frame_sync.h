#ifndef KRAKATOA_FRAME_SYNC_H
#define KRAKATOA_FRAME_SYNC_H
#include <vulkan/vulkan.h>
#include <vector>
#include "ring_buffer.h"
namespace graphics {

    /**
     * Manages per-frame synchronization primitives.
     *
     * Fences: per frame in flight (ring buffered). Controls CPU-GPU sync.
     *
     * Semaphores (acquire + renderFinished): per swapchain image.
     * This avoids the reuse hazard when frames_in_flight < swapchain_image_count.
     * The acquire semaphore is cycled with its own counter (we don't know
     * the image index before acquire). The renderFinished semaphore is indexed
     * by the acquired image index.
     *
     * Additionally, imagesInFlight tracks which fence is associated with each
     * swapchain image, so we can wait if a specific image is still in use.
     *
     * Usage:
     *   frameSync.AdvanceFrame();
     *   frameSync.WaitForCurrentFrame();
     *
     *   VkSemaphore acquireSem = frameSync.GetNextAcquireSemaphore();
     *   vkAcquireNextImageKHR(..., acquireSem, VK_NULL_HANDLE, &imageIndex);
     *
     *   frameSync.WaitForImage(imageIndex);
     *   frameSync.SetImageFence(imageIndex, frameSync.GetInFlightFence());
     *   frameSync.ResetCurrentFence();
     *
     *   // record commands...
     *
     *   VkSemaphore renderSem = frameSync.GetRenderFinishedSemaphore(imageIndex);
     *   // submit: wait acquireSem, signal renderSem, signal inFlightFence
     *   // present: wait renderSem
     */
    class FrameSync {
    public:
        FrameSync(VkDevice device, uint32_t swapchainImageCount);
        ~FrameSync();

        FrameSync(const FrameSync&) = delete;
        FrameSync& operator=(const FrameSync&) = delete;

        void RecreateForSwapchain(uint32_t newSwapchainImageCount);

        void AdvanceFrame();
        void WaitForCurrentFrame();
        void ResetCurrentFence();

        /// Get the next acquire semaphore (cycled independently of image index)
        VkSemaphore GetNextAcquireSemaphore();

        /// Get the render finished semaphore for a specific swapchain image
        VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const {
            return renderFinishedSemaphores[imageIndex];
        }

        VkFence GetInFlightFence() const {
            return inFlightFences.Current();
        }

        void WaitForImage(uint32_t imageIndex);
        void SetImageFence(uint32_t imageIndex, VkFence fence);

    private:
        VkDevice device;

        // Per frame in flight
        utils::RingBuffer<VkFence> inFlightFences;

        // Per swapchain image
        std::vector<VkSemaphore> acquireSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> imagesInFlight;
        uint32_t acquireSemaphoreIndex = 0;

        void CreatePerImageSyncObjects(uint32_t count);
        void DestroyPerImageSyncObjects();
    };
}
#endif //KRAKATOA_FRAME_SYNC_H