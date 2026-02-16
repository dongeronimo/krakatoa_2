#include <jni.h>
#include <string>
#include <cassert>
#include <android/native_window_jni.h>
#include <memory>
#include "android_log.h"
#include "ar_loader.h"
#include "vk_context.h"
#include "swap_chain_render_pass.h"
#include "pipeline.h"
#include "pipeline_layout.h"
#include <unordered_map>
#include "asset_loader.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include "command_pool_manager.h"
#include "frame_sync.h"
#include "mesh_loader.h"
#include "static_mesh.h"
#include "rdo.h"
#include "renderable.h"
#include "transform.h"
#include "frame_timer.h"
std::unique_ptr<graphics::VkContext> gVkContext = nullptr;
std::unique_ptr<graphics::SwapchainRenderPass> gSwapChainRenderPass = nullptr;
std::unique_ptr<graphics::Pipeline> gUnshadedOpaquePipeline = nullptr;
std::unordered_map<std::string, VkPipelineLayout> pipelineLayouts;
std::unordered_map<std::string, VkDescriptorSetLayout> descriptorSetLayouts;
std::unique_ptr<graphics::CommandPoolManager> gCommandPoolManager = nullptr;
std::unique_ptr<graphics::FrameSync> gFrameSync = nullptr;
std::unordered_map<std::string, std::unique_ptr<graphics::StaticMesh>> gStaticMeshes;
std::unique_ptr<graphics::FrameTimer> gFrameTimer = nullptr;

graphics::Renderable cube("cube");
extern "C" JNIEXPORT jstring JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnSurfaceCreated(JNIEnv *env,
                                                                                    jobject thiz,
                                                                                    jobject surface,
                                                                                    jobject asset_manager) {
    bool loadedArcore = ar::LoadARCore();

    AAssetManager* nativeAssetManager = AAssetManager_fromJava(env, asset_manager);
    assert(nativeAssetManager!= nullptr);//i MUST have the asset loader
    io::AssetLoader::initialize(nativeAssetManager);

    assert(loadedArcore);//i need arcore.
    // Create vulkan context (instance, physical device, device, semaphores, pipelines)
    gVkContext = std::make_unique<graphics::VkContext>();
    bool initializedOk = gVkContext->Initialize();
    assert(initializedOk);
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    bool surfaceOk = gVkContext->CreateSurface(window);
    assert(surfaceOk);
    gVkContext->CreateSwapchain(ANativeWindow_getWidth(window), ANativeWindow_getHeight(window));
    // Create swap chain render pass
    gSwapChainRenderPass = std::make_unique<graphics::SwapchainRenderPass>(gVkContext->GetDevice(),
                                                                           gVkContext->GetAllocator(),
                                                                           gVkContext->GetSwapchainFormat());
    auto unshadedOpaqueDescriptorSetLayout = graphics::DescriptorSetLayoutBuilder(gVkContext->GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
            .Build();
    descriptorSetLayouts.insert({"unshaded_opaque", unshadedOpaqueDescriptorSetLayout});
    auto unshadedOpaquePipelineLayout = graphics::PipelineLayoutBuilder(gVkContext->GetDevice())
            .AddDescriptorSetLayout(unshadedOpaqueDescriptorSetLayout)
            .Build();
    pipelineLayouts.insert({"unshaded_opaque", unshadedOpaquePipelineLayout});
    ANativeWindow_release(window);
    //Creates the command pool manager
    gCommandPoolManager = std::make_unique<graphics::CommandPoolManager>(gVkContext->GetDevice(),
                                                                         gVkContext->getQueueFamilies(),
                                                                         gVkContext->getGraphicsQueue(),
                                                                         gVkContext->getComputeQueue(),
                                                                         gVkContext->getTransferQueue());
    //creates the frame sync object
    gFrameSync = std::make_unique<graphics::FrameSync>(gVkContext->GetDevice(), gVkContext->getSwapchainImageCount());
    //Load meshes
    {
        io::MeshLoader meshLoader;
        auto meshData = meshLoader.Load("meshes/cube.glb");
        if (!meshData.vertices.empty() && !meshData.indices.empty()) {
            gStaticMeshes["cube"] = std::make_unique<graphics::StaticMesh>(
                    gVkContext->GetDevice(),
                    gVkContext->GetAllocator(),
                    *gCommandPoolManager,
                    meshData.vertices.data(),
                    meshData.vertexCount,
                    meshData.indices.data(),
                    meshData.indexCount,
                    "cube");
        }
    }
    cube.SetMesh(gStaticMeshes["cube"].get());
    gFrameTimer = std::make_unique<graphics::FrameTimer>();
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnSurfaceChanged(JNIEnv *env,
                                                                                    jobject thiz,
                                                                                    jint width,
                                                                                    jint height,
                                                                                    jint rotation) {
    vkDeviceWaitIdle(gVkContext->GetDevice());
    // Create the resources that rely on screen size
    if (gVkContext->GetSwapchain() == VK_NULL_HANDLE)
        gVkContext->CreateSwapchain(width, height);
    else
        gVkContext->RecreateSwapchain(width, height);
    gSwapChainRenderPass->Recreate(gVkContext->getSwapchainImageViews(),
                                  gVkContext->getSwapchainExtent());
    gUnshadedOpaquePipeline = std::make_unique<graphics::Pipeline>(gSwapChainRenderPass.get(),
                                                                   gVkContext->GetDevice(),
                                                                   gVkContext->GetAllocator(),
                                                                   graphics::UnshadedOpaqueConfig(),
                                                                   pipelineLayouts["unshaded_opaque"],
                                                                   descriptorSetLayouts["unshaded_opaque"]);
    gFrameSync->RecreateForSwapchain(gVkContext->getSwapchainImageCount());
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnSurfaceDestroyed(JNIEnv *env,
                                                                                      jobject thiz) {
    vkDeviceWaitIdle(gVkContext->GetDevice());
    gStaticMeshes.clear();
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnDrawFrame(JNIEnv *env,
                                                                               jobject thiz) {
    gFrameTimer->Tick();
    gFrameSync->AdvanceFrame();
    gCommandPoolManager->AdvanceFrame();

    gFrameSync->WaitForCurrentFrame();

    VkSemaphore acquireSem = gFrameSync->GetNextAcquireSemaphore();
    uint32_t imageIndex;
    vkAcquireNextImageKHR(gVkContext->GetDevice(), gVkContext->GetSwapchain(),
                          UINT64_MAX, acquireSem, VK_NULL_HANDLE, &imageIndex);

    gFrameSync->WaitForImage(imageIndex);
    gFrameSync->SetImageFence(imageIndex, gFrameSync->GetInFlightFence());
    gFrameSync->ResetCurrentFence();

    gCommandPoolManager->BeginFrame();
    VkCommandBuffer cmd = gCommandPoolManager->GetCurrentCommandBuffer();
    const uint32_t frameIndex = gVkContext->GetFrameIndex();
    gSwapChainRenderPass->setClearColor(1.0f, 0.0f, 0.0f, 0.0f);
    gSwapChainRenderPass->Begin(cmd,
                                gSwapChainRenderPass->GetFramebuffer(imageIndex),
                                gVkContext->getSwapchainExtent());
    ////////////////////////////////////////////////////////////////////////////////////////////////
    // TODO: Set camera
    glm::vec3 cameraPos = {3.0f, 5.0f, 7.0f};
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0,0,0), glm::vec3(0,1,0));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                      gSwapChainRenderPass->GetExtent().width/(float)gSwapChainRenderPass->GetExtent().height,
                                      0.1f, 100.0f);
    float dt = gFrameTimer->GetDeltaTime();
    cube.GetTransform().Rotate(glm::vec3(0, 45.0f * dt, 0));
    // TODO: Fill RDO
    graphics::RDO rdo;
    rdo.Add(graphics::RDO::Keys::COLOR, glm::vec4(0,1,0,1));
    rdo.Add(graphics::RDO::Keys::MODEL_MAT, cube.GetTransform().GetWorldMatrix());
    rdo.Add(graphics::RDO::Keys::VIEW_MAT, view);
    rdo.Add(graphics::RDO::Keys::PROJ_MAT, proj);
    // TODO: Draw the renderable with it's rdo
    gUnshadedOpaquePipeline->Bind(cmd);
    gUnshadedOpaquePipeline->Draw(cmd, &rdo, &cube, frameIndex);
    ////////////////////////////////////////////////////////////////////////////////////////////////
    gSwapChainRenderPass->End(cmd);
    gCommandPoolManager->EndFrame();

// Submit
    VkSemaphore waitSemaphores[] = {acquireSem};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore renderFinishedSem = gFrameSync->GetRenderFinishedSemaphore(imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSem;

    vkQueueSubmit(gVkContext->getGraphicsQueue(), 1, &submitInfo,
                  gFrameSync->GetInFlightFence());

// Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSem;
    presentInfo.swapchainCount = 1;
    auto swapchain = gVkContext->GetSwapchain();
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(gVkContext->getPresentQueue(), &presentInfo);
    gVkContext->Advance();
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeCleanup(JNIEnv *env,
                                                                           jobject thiz) {
    vkDeviceWaitIdle(gVkContext->GetDevice());
    gStaticMeshes.clear();
    for (const auto& [key, value] : descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(gVkContext->GetDevice(), value, nullptr);
    }

    for (const auto& [key, value] : pipelineLayouts)
    {
        vkDestroyPipelineLayout(gVkContext->GetDevice(), value, nullptr);
    }
    gUnshadedOpaquePipeline = nullptr;
    gCommandPoolManager = nullptr;
    gFrameSync = nullptr;
    gFrameTimer = nullptr;
    gVkContext = nullptr;
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnResume(JNIEnv *env,
                                                                            jobject thiz) {
    if (gFrameTimer) {
        gFrameTimer->Resume();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnPause(JNIEnv *env,
                                                                           jobject thiz) {
    if (gFrameTimer) {
        gFrameTimer->Pause();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeActivateArcore(JNIEnv *env,
                                                                                  jobject thiz,
                                                                                  jobject context,
                                                                                  jobject activity) {
    // TODO: implement nativeActivateArcore()
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnTouchEvent(JNIEnv *env,
                                                                                jobject thiz,
                                                                                jfloat x, jfloat y,
                                                                                jint action) {
    // TODO: implement nativeOnTouchEvent()
}