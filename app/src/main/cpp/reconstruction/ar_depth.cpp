#include "ar_manager.h"
#include "ar_depth.h"
#include "ar_depth_image.h"
#include <cassert>
using namespace reconstruction;

OnArDepthCreate ArDepth::gOnCreate;
VkDevice ArDepth::gDevice;
VmaAllocator ArDepth::gAllocator;

std::shared_ptr<ArDepth> gArDepth = nullptr;
std::shared_ptr<ArDepth> reconstruction::ArDepth::GetDepth(ar::ARSessionManager& gArSessionManager) {
    assert(HasVulkanThings());
    ArImage* depthImageHandle = gArSessionManager.getDepthImage();
    if(depthImageHandle != nullptr) {
        //Grab data from AR
        int32_t arDepthWidth = 0; int32_t arDepthHeight = 0;
        gArSessionManager.getDepthImageDimensions(depthImageHandle,
                                                   arDepthWidth, arDepthHeight);
        int32_t depthStride = 0; std::vector<uint16_t> depthData{};
        gArSessionManager.getDepthImageData(depthImageHandle, depthData, depthStride);
        ar::ArDepthIntrinsics arDepthIntrinsics{};
        gArSessionManager.getCameraIntrinsics(arDepthIntrinsics);
        gArSessionManager.releaseDepthImage(depthImageHandle);//must release the image
        if(gArDepth == nullptr){
            gArDepth = std::make_shared<ArDepth>(arDepthWidth, arDepthHeight, depthStride);
            gArDepth->UpdateImage(depthData, {(uint32_t)arDepthWidth, (uint32_t)arDepthHeight});
            gOnCreate(gArDepth.get());
        }
        gArDepth->depthData = depthData;
        gArDepth->arIntrinsics->fx = arDepthIntrinsics.fx;
        gArDepth->arIntrinsics->fy = arDepthIntrinsics.fy;
        gArDepth->arIntrinsics->cx = arDepthIntrinsics.cx;
        gArDepth->arIntrinsics->cy = arDepthIntrinsics.cy;

        assert(gArDepth->Width == arDepthWidth);
        return gArDepth;
    }
    else {
        return nullptr;
    }

}

ArDepth::~ArDepth() {
    gArDepthImage = nullptr;
}
ArDepth::ArDepth(int32_t width, int32_t height, int32_t stride) :
Width(width), Height(height), Stride(stride) {
    assert(HasVulkanThings());
    gArDepthImage = std::make_unique<graphics::ArDepthImage>(gDevice, gAllocator,"ArDepthImage");
    arIntrinsics = std::make_unique<ar::ArDepthIntrinsics>();
}
void ArDepth::UpdateWithMostRecent() {
    assert(HasVulkanThings());
    VkExtent2D d;// = {Width, Height};
    d.width = Width;
    d.height = Height;
    UpdateImage(depthData, d);
}
void ArDepth::UpdateImage(std::vector<uint16_t> &image, VkExtent2D dimensions) {
    assert(HasVulkanThings());
    gArDepthImage->Advance();
    gArDepthImage->UpdateImage(image, dimensions);
}

VkBuffer ArDepth::GetCurrentBuffer() {
    assert(HasVulkanThings());
    return gArDepthImage->GetCurrentBuffer();
}

void ArDepth::Release() {
    gArDepth = nullptr;

}

bool ArDepth::HasVulkanThings() {
    return gDevice && gAllocator;
}

const ar::ArDepthIntrinsics &ArDepth::GetInstrinsics() const {
    return *this->arIntrinsics.get();
}
