#include "frame_sync.h"
#include "vk_debug.h"
#include "android_log.h"
#include <cassert>
using namespace graphics;

FrameSync::FrameSync(VkDevice device, uint32_t swapchainImageCount)
        : device(device),
          inFlightFences(MAX_FRAMES_IN_FLIGHT) {

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult result = vkCreateFence(device, &fenceInfo, nullptr,
                                        &inFlightFences[i]);
        assert(result == VK_SUCCESS);
        debug::SetFenceName(device, inFlightFences[i],
                             Concatenate("InFlightFence[", i, "]"));
    }

    CreatePerImageSyncObjects(swapchainImageCount);

    LOGI("FrameSync created (%u frames in flight, %u swapchain images)",
         MAX_FRAMES_IN_FLIGHT, swapchainImageCount);
}

FrameSync::~FrameSync() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    DestroyPerImageSyncObjects();

    LOGI("FrameSync destroyed");
}

void FrameSync::CreatePerImageSyncObjects(uint32_t count) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    acquireSemaphores.resize(count);
    renderFinishedSemaphores.resize(count);
    imagesInFlight.resize(count, VK_NULL_HANDLE);
    acquireSemaphoreIndex = 0;

    for (uint32_t i = 0; i < count; i++) {
        VkResult result;

        result = vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                   &acquireSemaphores[i]);
        assert(result == VK_SUCCESS);
        debug::SetSemaphoreName(device, acquireSemaphores[i],
                             Concatenate("AcquireSemaphore[", i, "]"));

        result = vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                   &renderFinishedSemaphores[i]);
        assert(result == VK_SUCCESS);
        debug::SetSemaphoreName(device, renderFinishedSemaphores[i],
                             Concatenate("RenderFinishedSemaphore[", i, "]"));
    }
}

void FrameSync::DestroyPerImageSyncObjects() {
    for (auto sem : acquireSemaphores) {
        vkDestroySemaphore(device, sem, nullptr);
    }
    for (auto sem : renderFinishedSemaphores) {
        vkDestroySemaphore(device, sem, nullptr);
    }
    acquireSemaphores.clear();
    renderFinishedSemaphores.clear();
    imagesInFlight.clear();
}

void FrameSync::RecreateForSwapchain(uint32_t newSwapchainImageCount) {
    DestroyPerImageSyncObjects();
    CreatePerImageSyncObjects(newSwapchainImageCount);
    LOGI("FrameSync recreated for %u swapchain images", newSwapchainImageCount);
}

void FrameSync::AdvanceFrame() {
    inFlightFences.Next();
}

void FrameSync::WaitForCurrentFrame() {
    vkWaitForFences(device, 1, &inFlightFences.Current(),
                    VK_TRUE, UINT64_MAX);
}

void FrameSync::ResetCurrentFence() {
    vkResetFences(device, 1, &inFlightFences.Current());
}

VkSemaphore FrameSync::GetNextAcquireSemaphore() {
    VkSemaphore sem = acquireSemaphores[acquireSemaphoreIndex];
    acquireSemaphoreIndex = (acquireSemaphoreIndex + 1) %
                            static_cast<uint32_t>(acquireSemaphores.size());
    return sem;
}

void FrameSync::WaitForImage(uint32_t imageIndex) {
    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &imagesInFlight[imageIndex],
                        VK_TRUE, UINT64_MAX);
    }
}

void FrameSync::SetImageFence(uint32_t imageIndex, VkFence fence) {
    imagesInFlight[imageIndex] = fence;
}