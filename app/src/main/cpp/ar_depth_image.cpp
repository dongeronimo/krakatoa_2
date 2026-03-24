#include "ar_depth_image.h"
#include "vk_debug.h"
#include "concatenate.h"
#include <vulkan/vulkan.h>
#include <cassert>
#include <cstring>
using namespace graphics;

ArDepthImage::ArDepthImage(VkDevice device, VmaAllocator allocator,
                           const std::string &name) :
                           device(device), allocator(allocator), name(name) {
    for(auto i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
        depthBuffers[i] = VK_NULL_HANDLE;
        depthBufferAllocations[i] = VK_NULL_HANDLE;
        imageSizes[i] = {0,0};
        slotGeneration[i] = 0;
    }
}

ArDepthImage::~ArDepthImage() {
    for(auto i=0; i<MAX_FRAMES_IN_FLIGHT;i++) {
        if(depthBuffers[i] != VK_NULL_HANDLE){
            vmaDestroyBuffer(allocator, depthBuffers[i], depthBufferAllocations[i]);
        }
    }
}

void ArDepthImage::Advance() {
    depthBuffers.Next();
    depthBufferAllocations.Next();
    imageSizes.Next();
    slotGeneration.Next();
    if(slotGeneration.Current() < pendingGeneration) {
        UpdateCurrentSlotIfPending();
        slotGeneration.Current() = pendingGeneration;
    }
}

void ArDepthImage::UpdateImage(std::vector<uint16_t>& image, VkExtent2D dimensions) {
    bool same = pendingImage == image;
    if(same) return;
    pendingImage.assign(image.begin(), image.end());
    pendingDimension = dimensions;
    pendingGeneration++;
}

void ArDepthImage::UpdateCurrentSlotIfPending() {
    if(depthBuffers.Current() != VK_NULL_HANDLE){
        vmaDestroyBuffer(allocator, depthBuffers.Current(), depthBufferAllocations.Current());
    }
    UploadToCurrentSlot();
    SetObjectsNames();
}

void ArDepthImage::UploadToCurrentSlot() {
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pendingImage.size()) * sizeof(uint16_t);

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                      | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapInfo;
    VkResult result = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                      &depthBuffers.Current(),
                                      &depthBufferAllocations.Current(),
                                      &mapInfo);
    assert(result == VK_SUCCESS);

    memcpy(mapInfo.pMappedData, pendingImage.data(), bufferSize);

    imageSizes.Current() = pendingDimension;
}

void ArDepthImage::SetObjectsNames() {
    debug::SetBufferName(device, depthBuffers.Current(),
                         Concatenate(name, " Depth Buffer Generation #", pendingGeneration));
}
