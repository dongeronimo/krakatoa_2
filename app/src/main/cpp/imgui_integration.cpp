#include "imgui_integration.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "android_log.h"
#include <cassert>
#include <mutex>

namespace imgui_integration {

// ── Internal state ──────────────────────────────────────────────────────────
static VkDescriptorPool sDescriptorPool = VK_NULL_HANDLE;
static VkDevice         sDevice         = VK_NULL_HANDLE;
static bool             sInitialized    = false;

// Touch state — written from UI thread, read from render thread.
static std::mutex       sTouchMutex;
static float            sTouchX    = 0.0f;
static float            sTouchY    = 0.0f;
static bool             sTouchDown = false;

// ── Public API ──────────────────────────────────────────────────────────────

void Init(const InitInfo& info) {
    assert(!sInitialized && "imgui_integration::Init called twice");

    sDevice = info.device;

    // ── Dedicated descriptor pool for ImGui ─────────────────────────────
    // ImGui Vulkan backend allocates descriptor sets for font texture and
    // any user textures added via ImGui_ImplVulkan_AddTexture().
    // FREE_DESCRIPTOR_SET_BIT is required so RemoveTexture can free sets.
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
    };
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCI.maxSets       = 100;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = poolSizes;
    VkResult r = vkCreateDescriptorPool(sDevice, &poolCI, nullptr, &sDescriptorPool);
    assert(r == VK_SUCCESS);

    // ── ImGui context ───────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io  = ImGui::GetIO();
    io.DisplaySize = ImVec2(info.displayWidth, info.displayHeight);
    // We handle input ourselves — no platform backend.
    io.BackendPlatformName = "krakatoa_android";

    // ── Style: scale for high-DPI mobile screens ────────────────────────
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    // Use display width as a heuristic: most Android phones are >= 1080px wide.
    float scaleFactor = (info.displayWidth > 1000.0f) ? 3.0f : 2.0f;
    style.ScaleAllSizes(scaleFactor);
    style.TouchExtraPadding = ImVec2(10.0f, 10.0f);
    io.FontGlobalScale = scaleFactor;

    // ── Vulkan backend ──────────────────────────────────────────────────
    ImGui_ImplVulkan_InitInfo vkInfo{};
    vkInfo.Instance        = info.instance;
    vkInfo.PhysicalDevice  = info.physicalDevice;
    vkInfo.Device          = info.device;
    vkInfo.QueueFamily     = info.queueFamily;
    vkInfo.Queue           = info.graphicsQueue;
    vkInfo.DescriptorPool  = sDescriptorPool;
    vkInfo.RenderPass      = info.renderPass;
    vkInfo.MinImageCount   = 2;
    vkInfo.ImageCount      = info.imageCount;
    vkInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    vkInfo.Subpass         = 0;
    ImGui_ImplVulkan_Init(&vkInfo);

    // Upload font atlas to GPU. In ImGui 1.90+ this handles command buffer
    // submission internally.
    ImGui_ImplVulkan_CreateFontsTexture();

    sInitialized = true;
    LOGI("[ImGui] Initialized (%.0fx%.0f, scale=%.1f)", info.displayWidth, info.displayHeight, scaleFactor);
}

void Shutdown() {
    if (!sInitialized) return;

    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();

    if (sDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(sDevice, sDescriptorPool, nullptr);
        sDescriptorPool = VK_NULL_HANDLE;
    }

    sDevice      = VK_NULL_HANDLE;
    sInitialized = false;
    LOGI("[ImGui] Shutdown");
}

void OnSurfaceChanged(float width, float height) {
    if (!sInitialized) return;
    ImGuiIO& io  = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
}

void OnTouchEvent(float x, float y, int action) {
    std::lock_guard<std::mutex> lock(sTouchMutex);
    sTouchX = x;
    sTouchY = y;
    switch (action) {
        case 0: // DOWN
            sTouchDown = true;
            break;
        case 1: // MOVE
            // sTouchDown stays as-is (already true from DOWN)
            break;
        case 2: // UP
            sTouchDown = false;
            break;
        default:
            break;
    }
}

void NewFrame(float deltaSeconds) {
    if (!sInitialized) return;

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = (deltaSeconds > 0.0f) ? deltaSeconds : (1.0f / 60.0f);

    // Copy touch state under lock
    {
        std::lock_guard<std::mutex> lock(sTouchMutex);
        io.AddMousePosEvent(sTouchX, sTouchY);
        io.AddMouseButtonEvent(0, sTouchDown);
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void Render(VkCommandBuffer cmd) {
    if (!sInitialized) return;
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

VkDescriptorSet AddTexture(VkSampler sampler, VkImageView imageView,
                           VkImageLayout layout) {
    assert(sInitialized);
    return ImGui_ImplVulkan_AddTexture(sampler, imageView, layout);
}

void RemoveTexture(VkDescriptorSet descriptorSet) {
    if (!sInitialized) return;
    ImGui_ImplVulkan_RemoveTexture(descriptorSet);
}

bool IsInitialized() {
    return sInitialized;
}

} // namespace imgui_integration
