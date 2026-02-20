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
#include "ar_manager.h"
#include "egl_dummy_context.h"
#include "offscreen_render_pass.h"
#include "ar_camera_image.h"
#include "mesh.h"
std::unique_ptr<graphics::VkContext> gVkContext = nullptr;
std::unique_ptr<graphics::SwapchainRenderPass> gSwapChainRenderPass = nullptr;
std::unique_ptr<graphics::OffscreenRenderPass> gOffscreenRenderPass = nullptr;
std::unique_ptr<graphics::Pipeline> gUnshadedOpaquePipeline = nullptr;
std::unique_ptr<graphics::Pipeline> gCameraBgPipeline = nullptr;
std::unordered_map<std::string, VkPipelineLayout> pipelineLayouts;
std::unordered_map<std::string, VkDescriptorSetLayout> descriptorSetLayouts;
std::unique_ptr<graphics::CommandPoolManager> gCommandPoolManager = nullptr;
std::unique_ptr<graphics::FrameSync> gFrameSync = nullptr;
std::unordered_map<std::string, std::unique_ptr<graphics::Mesh>> gMeshes;
std::unique_ptr<graphics::FrameTimer> gFrameTimer = nullptr;
std::unique_ptr<ar::ARSessionManager> gArSessionManager = nullptr;
std::unique_ptr<graphics::ARCameraImage> gCameraImage = nullptr;
//dummy egl context do deal with arcore bullshit. use it before getting each ar frame.
ar::EglDummyContext m_eglDummy;
int gDisplayRotation = 0;
graphics::Renderable cube("cube");
graphics::Renderable cameraBgQuad("camera_bg");
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
                                                                                    jobject asset_manager,
                                                                                    jobject activity) {
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
    gOffscreenRenderPass = std::make_unique<graphics::OffscreenRenderPass>(gVkContext->GetDevice(),
                                                                           gVkContext->GetAllocator(),
                                                                           100, 100);
    auto unshadedOpaqueDescriptorSetLayout = graphics::DescriptorSetLayoutBuilder(gVkContext->GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
            .Build();
    descriptorSetLayouts.insert({"unshaded_opaque", unshadedOpaqueDescriptorSetLayout});
    auto unshadedOpaquePipelineLayout = graphics::PipelineLayoutBuilder(gVkContext->GetDevice())
            .AddDescriptorSetLayout(unshadedOpaqueDescriptorSetLayout)
            .Build();
    pipelineLayouts.insert({"unshaded_opaque", unshadedOpaquePipelineLayout});
    // Camera background: UBO (binding 0) + Y sampler (binding 1) + UV sampler (binding 2)
    auto cameraBgDescriptorSetLayout = graphics::DescriptorSetLayoutBuilder(gVkContext->GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build();
    descriptorSetLayouts.insert({"camera_bg", cameraBgDescriptorSetLayout});
    auto cameraBgPipelineLayout = graphics::PipelineLayoutBuilder(gVkContext->GetDevice())
            .AddDescriptorSetLayout(cameraBgDescriptorSetLayout)
            .Build();
    pipelineLayouts.insert({"camera_bg", cameraBgPipelineLayout});
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
            gMeshes["cube"] = std::make_unique<graphics::StaticMesh>(
                    gVkContext->GetDevice(),
                    gVkContext->GetAllocator(),
                    *gCommandPoolManager,
                    meshData.vertices.data(),
                    meshData.vertexCount,
                    meshData.indices.data(),
                    meshData.indexCount,
                    "cube");
        }
        auto quadData = io::MeshLoader::CreateFullscreenQuad();
        gMeshes["fullscreen_quad"] = std::make_unique<graphics::StaticMesh>(
                gVkContext->GetDevice(),
                gVkContext->GetAllocator(),
                *gCommandPoolManager,
                quadData.vertices.data(),
                quadData.vertexCount,
                quadData.indices.data(),
                quadData.indexCount,
                "fullscreen_quad");
    }
    //load the meshes
    cube.SetMesh(gMeshes["cube"].get());
    cameraBgQuad.SetMesh(gMeshes["fullscreen_quad"].get());
    //create the frame timer
    gFrameTimer = std::make_unique<graphics::FrameTimer>();
    //dummy egl context to deal with ar session bullshit

    m_eglDummy.initialize();
    //the ar session manager
    gArSessionManager = std::make_unique<ar::ARSessionManager>();
    gArSessionManager->initialize(env, activity, activity);
    gArSessionManager->onResume();
    //camera feed -> vulkan image (ring buffered, CPU upload, no OES)
    gCameraImage = std::make_unique<graphics::ARCameraImage>(gVkContext->GetDevice(),
                                                              gVkContext->GetAllocator());
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnSurfaceChanged(JNIEnv *env,
                                                                                    jobject thiz,
                                                                                    jint width,
                                                                                    jint height,
                                                                                    jint rotation) {
    vkDeviceWaitIdle(gVkContext->GetDevice());
    gDisplayRotation = rotation;
    gArSessionManager->onSurfaceChanged(rotation, width, height);
    // Create the resources that rely on screen size
    if (gVkContext->GetSwapchain() == VK_NULL_HANDLE)
        gVkContext->CreateSwapchain(width, height);
    else
        gVkContext->RecreateSwapchain(width, height);
    gSwapChainRenderPass->Recreate(gVkContext->getSwapchainImageViews(),
                                  gVkContext->getSwapchainExtent());
    gOffscreenRenderPass->Resize(gVkContext->getSwapchainExtent().width,
                                 gVkContext->getSwapchainExtent().height);
    gUnshadedOpaquePipeline = std::make_unique<graphics::Pipeline>(gOffscreenRenderPass.get(),
                                                                   gVkContext->GetDevice(),
                                                                   gVkContext->GetAllocator(),
                                                                   graphics::UnshadedOpaqueConfig(),
                                                                   pipelineLayouts["unshaded_opaque"],
                                                                   descriptorSetLayouts["unshaded_opaque"]);
    gCameraBgPipeline = std::make_unique<graphics::Pipeline>(gSwapChainRenderPass.get(),
                                                              gVkContext->GetDevice(),
                                                              gVkContext->GetAllocator(),
                                                              graphics::CameraBackgroundConfig(
                                                                      gCameraImage.get(),
                                                                      &gDisplayRotation),
                                                              pipelineLayouts["camera_bg"],
                                                              descriptorSetLayouts["camera_bg"]);
    gFrameSync->RecreateForSwapchain(gVkContext->getSwapchainImageCount());
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnSurfaceDestroyed(JNIEnv *env,
                                                                                      jobject thiz) {
    vkDeviceWaitIdle(gVkContext->GetDevice());
    gMeshes.clear();
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnDrawFrame(JNIEnv *env,
                                                                               jobject thiz) {
    gFrameTimer->Tick();
    gFrameSync->AdvanceFrame();
    gCommandPoolManager->AdvanceFrame();
    gCameraImage->AdvanceFrame();
    // Update ARCore first - acquires CPU camera image (YUV planes)
    m_eglDummy.makeCurrent();
    gArSessionManager->onDrawFrame();
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
    // Upload camera feed (YUV->RGBA) into the ring-buffered Vulkan image.
    // After this call the current image is in SHADER_READ_ONLY_OPTIMAL, ready to sample.
    gCameraImage->Update(cmd, gArSessionManager->getCameraFrame());
    ////////////////////// Update objects data /////////////////////////////////////////////////////
    //Set camera
    glm::vec3 cameraPos = {3.0f, 5.0f, 7.0f};
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0,0,0), glm::vec3(0,1,0));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                      gSwapChainRenderPass->GetExtent().width/(float)gSwapChainRenderPass->GetExtent().height,
                                      0.1f, 100.0f);
    float dt = gFrameTimer->GetDeltaTime();
    cube.GetTransform().Rotate(glm::vec3(0, 45.0f * dt, 0));
    ////////////////////////////////////////////////////////////////////////////////////////////////

    //begin the offscreen render pass
    gOffscreenRenderPass->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gOffscreenRenderPass->AdvanceFrame();
    gOffscreenRenderPass->Begin(cmd, gOffscreenRenderPass->GetFramebuffer(), gOffscreenRenderPass->GetExtent());
    // Fill RDO for the cube
    graphics::RDO rdo;
    rdo.Add(graphics::RDO::Keys::COLOR, glm::vec4(0,1,0,1));
    rdo.Add(graphics::RDO::Keys::MODEL_MAT, cube.GetTransform().GetWorldMatrix());
    rdo.Add(graphics::RDO::Keys::VIEW_MAT, view);
    rdo.Add(graphics::RDO::Keys::PROJ_MAT, proj);
    // Draw the cube using the unshaded pipeline.
    gUnshadedOpaquePipeline->Bind(cmd);
    gUnshadedOpaquePipeline->Draw(cmd, &rdo, &cube, frameIndex);
    gOffscreenRenderPass->End(cmd);
    //begin the swap chain render pass
    gSwapChainRenderPass->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gSwapChainRenderPass->Begin(cmd,
                                gSwapChainRenderPass->GetFramebuffer(imageIndex),
                                gVkContext->getSwapchainExtent());
    // Draw camera background (fullscreen quad with camera texture, depth=1.0)
    if (gCameraImage->IsValid()) {
        gCameraBgPipeline->Bind(cmd);
        gCameraBgPipeline->Draw(cmd, nullptr, &cameraBgQuad, frameIndex);
    }
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
    gCameraImage = nullptr;
    gArSessionManager.release();
    gMeshes.clear();
    for (const auto& [key, value] : descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(gVkContext->GetDevice(), value, nullptr);
    }

    for (const auto& [key, value] : pipelineLayouts)
    {
        vkDestroyPipelineLayout(gVkContext->GetDevice(), value, nullptr);
    }
    gCameraBgPipeline = nullptr;
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
    if(gArSessionManager)
        gArSessionManager->onResume();
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnPause(JNIEnv *env,
                                                                           jobject thiz) {
    if (gFrameTimer) {
        gFrameTimer->Pause();
    }
    gArSessionManager->onPause();
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
extern "C"
JNIEXPORT jintArray JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeGetAvailableResolutions(
        JNIEnv *env, jobject thiz) {
    if (!gArSessionManager) return nullptr;
    const auto& resolutions = gArSessionManager->getAvailableResolutions();
    // Return flat array: [w0, h0, w1, h1, ...]
    jintArray result = env->NewIntArray(static_cast<jsize>(resolutions.size() * 2));
    if (!result) return nullptr;
    std::vector<jint> flat(resolutions.size() * 2);
    for (size_t i = 0; i < resolutions.size(); ++i) {
        flat[i * 2]     = resolutions[i].width;
        flat[i * 2 + 1] = resolutions[i].height;
    }
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(flat.size()), flat.data());
    return result;
}
extern "C"
JNIEXPORT jint JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeGetCurrentResolutionIndex(
        JNIEnv *env, jobject thiz) {
    if (!gArSessionManager) return -1;
    return gArSessionManager->getCurrentResolutionIndex();
}
extern "C"
JNIEXPORT jboolean JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeSetResolution(
        JNIEnv *env, jobject thiz, jint index) {
    if (!gArSessionManager) return JNI_FALSE;
    return gArSessionManager->setResolution(index) ? JNI_TRUE : JNI_FALSE;
}