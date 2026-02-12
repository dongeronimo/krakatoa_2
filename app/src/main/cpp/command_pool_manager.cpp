#include "command_pool_manager.h"
#include "android_log.h"
#include <cassert>
#include <set>
using namespace graphics;

CommandPoolManager::CommandPoolManager(VkDevice device,
                                       const QueueFamilyIndices& queueFamilies,
                                       VkQueue graphicsQueue,
                                       VkQueue computeQueue,
                                       VkQueue transferQueue)
        : device(device),
          graphicsQueue(graphicsQueue),
          computeQueue(computeQueue),
          transferQueue(transferQueue),
          graphicsFamilyIndex(queueFamilies.graphicsFamily.value()),
          computeFamilyIndex(queueFamilies.computeFamily.value()),
          transferFamilyIndex(queueFamilies.transferFamily.value()),
          frameCommandBuffers(MAX_FRAMES_IN_FLIGHT) {

    // Graphics pool: RESET_COMMAND_BUFFER because frame buffers are reused
    graphicsPool = CreatePool(graphicsFamilyIndex,
                              VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    LOGI("Graphics command pool created (family %u)", graphicsFamilyIndex);

    // Compute pool
    if (computeFamilyIndex != graphicsFamilyIndex) {
        computePool = CreatePool(computeFamilyIndex,
                                 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        LOGI("Compute command pool created (family %u)", computeFamilyIndex);
    } else {
        computePool = graphicsPool;
        LOGI("Compute shares graphics command pool (family %u)", computeFamilyIndex);
    }

    // Transfer pool: TRANSIENT for short-lived one-shot buffers
    if (transferFamilyIndex != graphicsFamilyIndex &&
        transferFamilyIndex != computeFamilyIndex) {
        transferPool = CreatePool(transferFamilyIndex,
                                  VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
        LOGI("Transfer command pool created (family %u)", transferFamilyIndex);
    } else if (transferFamilyIndex == computeFamilyIndex && computePool != graphicsPool) {
        transferPool = computePool;
        LOGI("Transfer shares compute command pool (family %u)", transferFamilyIndex);
    } else {
        transferPool = graphicsPool;
        LOGI("Transfer shares graphics command pool (family %u)", transferFamilyIndex);
    }

    AllocateFrameCommandBuffers();

    LOGI("CommandPoolManager created (%u frame cmd buffers, dedicated transfer: %s)",
         MAX_FRAMES_IN_FLIGHT, HasDedicatedTransfer() ? "YES" : "NO");
}

CommandPoolManager::~CommandPoolManager() {
    std::set<VkCommandPool> uniquePools;
    if (graphicsPool != VK_NULL_HANDLE) uniquePools.insert(graphicsPool);
    if (computePool != VK_NULL_HANDLE) uniquePools.insert(computePool);
    if (transferPool != VK_NULL_HANDLE) uniquePools.insert(transferPool);

    for (auto pool : uniquePools) {
        vkDestroyCommandPool(device, pool, nullptr);
    }

    LOGI("CommandPoolManager destroyed");
}

// ============================================================
// Pool / buffer creation
// ============================================================

VkCommandPool CommandPoolManager::CreatePool(uint32_t queueFamilyIndex,
                                             VkCommandPoolCreateFlags flags) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = flags;

    VkCommandPool pool;
    VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &pool);
    assert(result == VK_SUCCESS);
    return pool;
}

void CommandPoolManager::AllocateFrameCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = graphicsPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = frameCommandBuffers.Size();

    std::vector<VkCommandBuffer> buffers(frameCommandBuffers.Size());
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, buffers.data());
    assert(result == VK_SUCCESS);

    for (uint32_t i = 0; i < frameCommandBuffers.Size(); i++) {
        frameCommandBuffers[i] = buffers[i];
    }

    LOGI("Allocated %u frame command buffers", frameCommandBuffers.Size());
}

// ============================================================
// Frame command buffers
// ============================================================

void CommandPoolManager::AdvanceFrame() {
    frameCommandBuffers.Next();
}

VkCommandBuffer CommandPoolManager::GetCurrentCommandBuffer() const {
    return frameCommandBuffers.Current();
}

void CommandPoolManager::BeginFrame() {
    VkCommandBuffer cmd = GetCurrentCommandBuffer();
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(cmd, &beginInfo);
    assert(result == VK_SUCCESS);
}

void CommandPoolManager::EndFrame() {
    VkCommandBuffer cmd = GetCurrentCommandBuffer();
    VkResult result = vkEndCommandBuffer(cmd);
    assert(result == VK_SUCCESS);
}

// ============================================================
// One-shot commands
// ============================================================

void CommandPoolManager::SubmitOneShot(QueueType queueType,
                                       const std::function<void(VkCommandBuffer)>& recordFunc) {
    VkCommandPool pool = GetPool(queueType);
    VkQueue queue = GetQueue(queueType);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &cmd);
    assert(result == VK_SUCCESS);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);
    recordFunc(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

// ============================================================
// Upload with queue family ownership transfer
// ============================================================

void CommandPoolManager::UploadBuffer(VkBuffer srcBuffer,
                                      VkBuffer dstBuffer,
                                      VkDeviceSize size,
                                      VkPipelineStageFlags dstStage,
                                      VkAccessFlags dstAccess) {
    bool needsOwnershipTransfer = HasDedicatedTransfer();

    // Step 1: Copy on transfer queue + release (if needed)
    SubmitOneShot(QueueType::Transfer, [&](VkCommandBuffer cmd) {
        // Copy
        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(cmd, srcBuffer, dstBuffer, 1, &region);

        if (needsOwnershipTransfer) {
            // Release barrier: transfer queue releases ownership
            VkBufferMemoryBarrier releaseBarrier{};
            releaseBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            releaseBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            releaseBarrier.dstAccessMask = 0; // dst is ignored in release
            releaseBarrier.srcQueueFamilyIndex = transferFamilyIndex;
            releaseBarrier.dstQueueFamilyIndex = graphicsFamilyIndex;
            releaseBarrier.buffer = dstBuffer;
            releaseBarrier.offset = 0;
            releaseBarrier.size = size;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0,
                                 0, nullptr,
                                 1, &releaseBarrier,
                                 0, nullptr);
        }
    });

    if (needsOwnershipTransfer) {
        // Step 2: Acquire on graphics queue
        SubmitOneShot(QueueType::Graphics, [&](VkCommandBuffer cmd) {
            VkBufferMemoryBarrier acquireBarrier{};
            acquireBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            acquireBarrier.srcAccessMask = 0; // src is ignored in acquire
            acquireBarrier.dstAccessMask = dstAccess;
            acquireBarrier.srcQueueFamilyIndex = transferFamilyIndex;
            acquireBarrier.dstQueueFamilyIndex = graphicsFamilyIndex;
            acquireBarrier.buffer = dstBuffer;
            acquireBarrier.offset = 0;
            acquireBarrier.size = size;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 dstStage,
                                 0,
                                 0, nullptr,
                                 1, &acquireBarrier,
                                 0, nullptr);
        });
    }
}

void CommandPoolManager::UploadImage(VkBuffer srcBuffer,
                                     VkImage dstImage,
                                     uint32_t width,
                                     uint32_t height,
                                     VkImageLayout finalLayout) {
    bool needsOwnershipTransfer = HasDedicatedTransfer();

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    // Step 1: Transition to TRANSFER_DST, copy, release (on transfer queue)
    SubmitOneShot(QueueType::Transfer, [&](VkCommandBuffer cmd) {
        // Transition UNDEFINED -> TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier toTransferDst{};
        toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransferDst.srcAccessMask = 0;
        toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst.image = dstImage;
        toTransferDst.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &toTransferDst);

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0; // tightly packed
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(cmd, srcBuffer, dstImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        if (needsOwnershipTransfer) {
            // Release: transfer queue gives up ownership
            // Layout stays TRANSFER_DST_OPTIMAL — graphics side will transition
            VkImageMemoryBarrier releaseBarrier{};
            releaseBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            releaseBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            releaseBarrier.dstAccessMask = 0;
            releaseBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            releaseBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            releaseBarrier.srcQueueFamilyIndex = transferFamilyIndex;
            releaseBarrier.dstQueueFamilyIndex = graphicsFamilyIndex;
            releaseBarrier.image = dstImage;
            releaseBarrier.subresourceRange = subresourceRange;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &releaseBarrier);
        }
    });

    // Step 2: Acquire on graphics queue + transition to final layout
    if (needsOwnershipTransfer) {
        SubmitOneShot(QueueType::Graphics, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier acquireBarrier{};
            acquireBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            acquireBarrier.srcAccessMask = 0;
            acquireBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            acquireBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            acquireBarrier.newLayout = finalLayout;
            acquireBarrier.srcQueueFamilyIndex = transferFamilyIndex;
            acquireBarrier.dstQueueFamilyIndex = graphicsFamilyIndex;
            acquireBarrier.image = dstImage;
            acquireBarrier.subresourceRange = subresourceRange;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &acquireBarrier);
        });
    } else {
        // Same family: just transition layout
        SubmitOneShot(QueueType::Graphics, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier toFinal{};
            toFinal.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toFinal.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toFinal.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toFinal.newLayout = finalLayout;
            toFinal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toFinal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toFinal.image = dstImage;
            toFinal.subresourceRange = subresourceRange;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &toFinal);
        });
    }
}

// ============================================================
// Helpers
// ============================================================

VkQueue CommandPoolManager::GetQueue(QueueType type) const {
    switch (type) {
        case QueueType::Graphics: return graphicsQueue;
        case QueueType::Compute:  return computeQueue;
        case QueueType::Transfer: return transferQueue;
    }
    assert(false && "Unknown queue type");
    return VK_NULL_HANDLE;
}

VkCommandPool CommandPoolManager::GetPool(QueueType type) const {
    switch (type) {
        case QueueType::Graphics: return graphicsPool;
        case QueueType::Compute:  return computePool;
        case QueueType::Transfer: return transferPool;
    }
    assert(false && "Unknown queue type");
    return VK_NULL_HANDLE;
}

uint32_t CommandPoolManager::GetFamilyIndex(QueueType type) const {
    switch (type) {
        case QueueType::Graphics: return graphicsFamilyIndex;
        case QueueType::Compute:  return computeFamilyIndex;
        case QueueType::Transfer: return transferFamilyIndex;
    }
    assert(false && "Unknown queue type");
    return 0;
}

VkCommandPool CommandPoolManager::GetCommandPool(QueueType queueType) const {
    return GetPool(queueType);
}