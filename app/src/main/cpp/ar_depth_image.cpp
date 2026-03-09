#include "ar_depth_image.h"
#include <vulkan/vulkan.h>
using namespace graphics;

ArDepthImage::ArDepthImage(VkDevice device, VmaAllocator allocator,
                           CommandPoolManager &cmdManager) :
                           device(device), allocator(allocator) {
    for(auto i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
        depthImages[i] = VK_NULL_HANDLE;
        depthImagesAllocations[i] = VK_NULL_HANDLE;
        depthImageViews[i] = VK_NULL_HANDLE;
        imageSizes[i] = {0,0};
        slotGeneration[i] = 0;
    }
}

ArDepthImage::~ArDepthImage() {
    for(auto i=0; i<MAX_FRAMES_IN_FLIGHT;i++) {
        if(depthImages[i] != VK_NULL_HANDLE){
            vkDestroyImageView(device, depthImageViews[i], nullptr);
            vmaDestroyImage(allocator, depthImages[i], depthImagesAllocations[i]);
        }
    }
}

void ArDepthImage::Advance() {
    depthImages.Next();
    depthImagesAllocations.Next();
    depthImageViews.Next();
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
    if(depthImages.Current() != VK_NULL_HANDLE){
        //there's something in this slot, delete it.
        vkDestroyImageView(device, depthImageViews.Current(), nullptr);
        vmaDestroyImage(allocator, depthImages.Current(), depthImagesAllocations.Current());
    }
    //TODO: make something equivalent to MutableMesh::FillBuffer to fill the image
    //TODO: create the image view
    //TODO: set the new object names
}
