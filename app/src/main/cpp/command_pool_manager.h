#ifndef KRAKATOA_COMMAND_POOL_MANAGER_H
#define KRAKATOA_COMMAND_POOL_MANAGER_H
#include <vulkan/vulkan.h>
#include <functional>
#include "ring_buffer.h"
#include "queue_family_indices.h"
namespace graphics {

    /**
     * Manages command pools (one per queue family) and command buffers.
     *
     * Provides:
     * - Ring-buffered command buffers for per-frame rendering
     * - One-shot command buffer execution on any queue
     * - Buffer/image upload with automatic queue family ownership transfer
     *
     * Usage (frame):
     *   cmdManager.AdvanceFrame();
     *   cmdManager.BeginFrame();
     *   VkCommandBuffer cmd = cmdManager.GetCurrentCommandBuffer();
     *   // record...
     *   cmdManager.EndFrame();
     *
     * Usage (one-shot):
     *   cmdManager.SubmitOneShot(QueueType::Transfer, [&](VkCommandBuffer cmd) {
     *       vkCmdCopyBuffer(cmd, src, dst, 1, &region);
     *   });
     *
     * Usage (buffer upload with ownership transfer):
     *   cmdManager.UploadBuffer(staging, gpu, size,
     *       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
     *
     * Usage (image upload with ownership transfer + layout transition):
     *   cmdManager.UploadImage(staging, image, width, height,
     *       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
     */
    class CommandPoolManager {
    public:
        enum class QueueType {
            Graphics,
            Compute,
            Transfer
        };

        CommandPoolManager(VkDevice device,
                           const QueueFamilyIndices& queueFamilies,
                           VkQueue graphicsQueue,
                           VkQueue computeQueue,
                           VkQueue transferQueue);
        ~CommandPoolManager();

        CommandPoolManager(const CommandPoolManager&) = delete;
        CommandPoolManager& operator=(const CommandPoolManager&) = delete;

        /// Advance to next frame's command buffer.
        void AdvanceFrame();

        /// Get the current frame's command buffer (graphics queue).
        VkCommandBuffer GetCurrentCommandBuffer() const;

        /// Begin recording the current frame's command buffer.
        void BeginFrame();

        /// End recording the current frame's command buffer.
        void EndFrame();

        /// Execute a one-shot command on the specified queue. Blocking.
        void SubmitOneShot(QueueType queueType,
                           const std::function<void(VkCommandBuffer)>& recordFunc);

        /**
         * Upload a buffer from staging to GPU via transfer queue.
         * Handles queue family ownership transfer if transfer and graphics
         * are on different families.
         *
         * @param srcBuffer  Staging buffer (host visible)
         * @param dstBuffer  GPU buffer
         * @param size       Bytes to copy
         * @param dstStage   Pipeline stage where the buffer will be consumed
         * @param dstAccess  Access mask for the destination usage
         */
        void UploadBuffer(VkBuffer srcBuffer,
                          VkBuffer dstBuffer,
                          VkDeviceSize size,
                          VkPipelineStageFlags dstStage,
                          VkAccessFlags dstAccess);

        /**
         * Upload an image from staging buffer to GPU image via transfer queue.
         * Handles layout transitions and queue family ownership transfer.
         * Image ends in finalLayout, ready for use on the graphics queue.
         *
         * @param srcBuffer   Staging buffer with pixel data
         * @param dstImage    GPU image
         * @param width       Image width
         * @param height      Image height
         * @param finalLayout Layout the image should be in after upload
         */
        void UploadImage(VkBuffer srcBuffer,
                         VkImage dstImage,
                         uint32_t width,
                         uint32_t height,
                         VkImageLayout finalLayout);

        VkCommandPool GetCommandPool(QueueType queueType) const;

        /// Whether transfer and graphics use different queue families
        bool HasDedicatedTransfer() const { return transferFamilyIndex != graphicsFamilyIndex; }

    private:
        VkDevice device;
        VkQueue graphicsQueue;
        VkQueue computeQueue;
        VkQueue transferQueue;

        uint32_t graphicsFamilyIndex;
        uint32_t computeFamilyIndex;
        uint32_t transferFamilyIndex;

        VkCommandPool graphicsPool = VK_NULL_HANDLE;
        VkCommandPool computePool = VK_NULL_HANDLE;
        VkCommandPool transferPool = VK_NULL_HANDLE;

        utils::RingBuffer<VkCommandBuffer> frameCommandBuffers;

        VkCommandPool CreatePool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags);
        void AllocateFrameCommandBuffers();

        VkQueue GetQueue(QueueType type) const;
        VkCommandPool GetPool(QueueType type) const;
        uint32_t GetFamilyIndex(QueueType type) const;
    };
}
#endif //KRAKATOA_COMMAND_POOL_MANAGER_H