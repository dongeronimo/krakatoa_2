#include "swap_chain_render_pass.h"
#include "vk_debug.h"
#include "android_log.h"
#include <cassert>
#include <array>
using namespace graphics;

SwapchainRenderPass::SwapchainRenderPass(VkDevice device,
                                         VmaAllocator allocator,
                                         VkFormat swapchainFormat,
                                         VkFormat depthFormat)
        : allocator(allocator),
          swapchainFormat(swapchainFormat),
          depthFormat(depthFormat) {
    this->device = device;
    this->debugName = "SwapchainRenderPass";

    // Clear values: color black, depth 1.0
    clearValues.resize(2);
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    CreateRenderPass();
    LOGI("SwapchainRenderPass created (render pass only, call Recreate() with swapchain images)");
}

SwapchainRenderPass::~SwapchainRenderPass() {
    DestroyFramebuffers();
    DestroyDepthImage();
    DestroyRenderPass();
    LOGI("SwapchainRenderPass destroyed");
}

void SwapchainRenderPass::Recreate(const std::vector<VkImageView>& swapchainImageViews,
                                   VkExtent2D newExtent) {
    LOGI("SwapchainRenderPass::Recreate %ux%u with %zu images",
         newExtent.width, newExtent.height, swapchainImageViews.size());

    // Destroy old resources
    DestroyFramebuffers();
    DestroyDepthImage();

    extent = newExtent;

    // Recreate with new dimensions
    CreateDepthImage();
    CreateFramebuffers(swapchainImageViews);
}

void SwapchainRenderPass::CreateRenderPass() {
    // --- Attachment descriptions ---

    // 0: Color (swapchain image)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // 1: Depth
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

    // --- Subpass ---
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

    // --- Subpass dependencies ---
    // Wait for the swapchain image to be available before writing color
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // --- Create ---
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
    debug::SetObjectName(device, renderPass, "SwapchainRenderPass");
    LOGI("Swapchain VkRenderPass created (format=%d)", swapchainFormat);
}

void SwapchainRenderPass::CreateDepthImage() {
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.format = depthFormat;
    depthImageInfo.extent = {extent.width, extent.height, 1};
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkResult result = vmaCreateImage(allocator, &depthImageInfo, &allocInfo,
                                     &depthImage, &depthAllocation, nullptr);
    assert(result == VK_SUCCESS);
    debug::SetObjectName(device, depthImage, "SwapchainDepthImage");

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(device, &viewInfo, nullptr, &depthImageView);
    assert(result == VK_SUCCESS);
    debug::SetObjectName(device, depthImageView, "SwapchainDepthImageView");

    LOGI("Swapchain depth image created (%ux%u)", extent.width, extent.height);
}

void SwapchainRenderPass::CreateFramebuffers(
        const std::vector<VkImageView>& swapchainImageViews) {
    framebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
                swapchainImageViews[i],  // color from swapchain
                depthImageView           // shared depth
        };

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers[i]);
        assert(result == VK_SUCCESS);
        debug::SetObjectName(device, framebuffers[i],
                             Concatenate("SwapchainFramebuffer[", i, "]"));
    }

    LOGI("Created %zu swapchain framebuffers (%ux%u)",
         framebuffers.size(), extent.width, extent.height);
}

void SwapchainRenderPass::DestroyDepthImage() {
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depthImage, depthAllocation);
        depthImage = VK_NULL_HANDLE;
        depthAllocation = VK_NULL_HANDLE;
    }
}

void SwapchainRenderPass::DestroyFramebuffers() {
    for (auto fb : framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
    }
    framebuffers.clear();
}