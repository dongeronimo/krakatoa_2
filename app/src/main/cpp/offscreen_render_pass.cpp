#include "offscreen_render_pass.h"
#include "android_log.h"
#include <cassert>
#include <array>
using namespace graphics;

OffscreenRenderPass::OffscreenRenderPass(VkDevice device,
                                         VmaAllocator allocator,
                                         uint32_t width,
                                         uint32_t height,
                                         VkFormat colorFormat,
                                         VkFormat depthFormat)
        : allocator(allocator),
          width(width),
          height(height),
          colorFormat(colorFormat),
          depthFormat(depthFormat),
          frameResources(MAX_FRAMES_IN_FLIGHT) {
    this->device = device;

    clearValues.resize(2);
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    CreateRenderPass();
    CreateImages();
    CreateFramebuffers();

    LOGI("OffscreenRenderPass created (%ux%u, %u frames in flight)",
         width, height, MAX_FRAMES_IN_FLIGHT);
}

OffscreenRenderPass::~OffscreenRenderPass() {
    DestroyFramebuffers();
    DestroyImages();
    DestroyRenderPass();
    LOGI("OffscreenRenderPass destroyed");
}

void OffscreenRenderPass::AdvanceFrame() {
    frameResources.Next();
}

void OffscreenRenderPass::Resize(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == width && newHeight == height) return;

    LOGI("OffscreenRenderPass resizing %ux%u -> %ux%u",
         width, height, newWidth, newHeight);

    width = newWidth;
    height = newHeight;

    DestroyFramebuffers();
    DestroyImages();
    CreateImages();
    CreateFramebuffers();
}

void OffscreenRenderPass::CreateRenderPass() {
    // 0: Color attachment
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // 1: Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(device, &createInfo, nullptr, &renderPass);
    assert(result == VK_SUCCESS);
    LOGI("Offscreen VkRenderPass created");
}

void OffscreenRenderPass::CreateImages() {
    for (uint32_t i = 0; i < frameResources.Size(); i++) {
        auto& res = frameResources[i];

        // --- Color image ---
        VkImageCreateInfo colorImageInfo{};
        colorImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorImageInfo.imageType = VK_IMAGE_TYPE_2D;
        colorImageInfo.format = colorFormat;
        colorImageInfo.extent = {width, height, 1};
        colorImageInfo.mipLevels = 1;
        colorImageInfo.arrayLayers = 1;
        colorImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        colorImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        colorImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT;
        colorImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        colorImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo colorAllocInfo{};
        colorAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult result = vmaCreateImage(allocator, &colorImageInfo, &colorAllocInfo,
                                         &res.colorImage, &res.colorAllocation, nullptr);
        assert(result == VK_SUCCESS);

        res.colorImageView = CreateImageView(res.colorImage, colorFormat,
                                             VK_IMAGE_ASPECT_COLOR_BIT);

        // --- Depth image ---
        VkImageCreateInfo depthImageInfo{};
        depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
        depthImageInfo.format = depthFormat;
        depthImageInfo.extent = {width, height, 1};
        depthImageInfo.mipLevels = 1;
        depthImageInfo.arrayLayers = 1;
        depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo depthAllocInfo{};
        depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        result = vmaCreateImage(allocator, &depthImageInfo, &depthAllocInfo,
                                &res.depthImage, &res.depthAllocation, nullptr);
        assert(result == VK_SUCCESS);

        res.depthImageView = CreateImageView(res.depthImage, depthFormat,
                                             VK_IMAGE_ASPECT_DEPTH_BIT);

        LOGI("Offscreen frame resources [%u] created (%ux%u)", i, width, height);
    }
}

void OffscreenRenderPass::CreateFramebuffers() {
    for (uint32_t i = 0; i < frameResources.Size(); i++) {
        auto& res = frameResources[i];

        std::array<VkImageView, 2> attachments = {res.colorImageView, res.depthImageView};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(device, &fbInfo, nullptr, &res.framebuffer);
        assert(result == VK_SUCCESS);
    }

    LOGI("Created %u offscreen framebuffers (%ux%u)",
         frameResources.Size(), width, height);
}

void OffscreenRenderPass::DestroyImages() {
    for (uint32_t i = 0; i < frameResources.Size(); i++) {
        auto& res = frameResources[i];

        if (res.colorImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, res.colorImageView, nullptr);
            res.colorImageView = VK_NULL_HANDLE;
        }
        if (res.colorImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, res.colorImage, res.colorAllocation);
            res.colorImage = VK_NULL_HANDLE;
            res.colorAllocation = VK_NULL_HANDLE;
        }
        if (res.depthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, res.depthImageView, nullptr);
            res.depthImageView = VK_NULL_HANDLE;
        }
        if (res.depthImage != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, res.depthImage, res.depthAllocation);
            res.depthImage = VK_NULL_HANDLE;
            res.depthAllocation = VK_NULL_HANDLE;
        }
    }
}

void OffscreenRenderPass::DestroyFramebuffers() {
    for (uint32_t i = 0; i < frameResources.Size(); i++) {
        auto& res = frameResources[i];
        if (res.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, res.framebuffer, nullptr);
            res.framebuffer = VK_NULL_HANDLE;
        }
    }
}

VkImageView OffscreenRenderPass::CreateImageView(VkImage image,
                                                 VkFormat format,
                                                 VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &imageView);
    assert(result == VK_SUCCESS);
    return imageView;
}