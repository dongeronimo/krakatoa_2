#include "render_pass.h"
#include "vk_debug.h"
#include "android_log.h"
#include <cassert>
using namespace graphics;

void RenderPass::Begin(VkCommandBuffer cmd,
                       VkFramebuffer framebuffer,
                       VkExtent2D extent) {
    if (!debugName.empty()) {
        debug::BeginLabel(cmd, debugName);
    }

    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = renderPass;
    beginInfo.framebuffer = framebuffer;
    beginInfo.renderArea.offset = {0, 0};
    beginInfo.renderArea.extent = extent;
    beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    beginInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set dynamic viewport and scissor to match extent
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void RenderPass::End(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);

    if (!debugName.empty()) {
        debug::EndLabel(cmd);
    }
}

void RenderPass::DestroyRenderPass() {
    if (renderPass != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}