#include <jni.h>
#include <string>
#include <cstring>
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
#include "mutable_mesh.h"
#include "concatenate.h"
#include "texture2d.h"
#include "image_load.h"
#include <glm/gtc/type_ptr.hpp>
#include "ar_depth_image.h"
#include "tsdf_volume.h"
#include "gpu_mesh.h"
#include "vk_debug.h"
#include "marching_cubes_op.h"
#include "tsdf_volume.h"
#include "tsdf_fusion_op.h"
#include "chisel_bridge/chisel_manager.h"
std::unique_ptr<graphics::VkContext> gVkContext = nullptr;
std::unique_ptr<graphics::SwapchainRenderPass> gSwapChainRenderPass = nullptr;
std::unique_ptr<graphics::OffscreenRenderPass> gOffscreenRenderPass = nullptr;
std::unique_ptr<graphics::Pipeline> gUnshadedOpaquePipeline = nullptr;
std::unique_ptr<graphics::Pipeline> gTransparentPhongPipeline = nullptr;
std::unique_ptr<graphics::Pipeline> gCameraBgPipeline = nullptr;
std::unique_ptr<graphics::Pipeline> gComposePipeline = nullptr;
std::unordered_map<std::string, VkPipelineLayout> pipelineLayouts;
std::unordered_map<std::string, VkDescriptorSetLayout> descriptorSetLayouts;
std::unique_ptr<graphics::CommandPoolManager> gCommandPoolManager = nullptr;
std::unique_ptr<graphics::FrameSync> gFrameSync = nullptr;
std::unordered_map<std::string, std::unique_ptr<graphics::Mesh>> gMeshes;
std::unique_ptr<graphics::FrameTimer> gFrameTimer = nullptr;
std::unique_ptr<ar::ARSessionManager> gArSessionManager = nullptr;
std::unique_ptr<graphics::ARCameraImage> gCameraImage = nullptr;
std::unique_ptr<graphics::Texture2D> gGridTexture = nullptr;
//dummy egl context do deal with arcore bullshit. use it before getting each ar frame.
ar::EglDummyContext m_eglDummy;
int gDisplayRotation = 0;
std::unique_ptr<graphics::Renderable> cameraBgQuad = nullptr;
std::unique_ptr<graphics::Renderable> composeQuad = nullptr;
std::unordered_map<int64_t, std::shared_ptr<graphics::Renderable>> gArPlanes;
std::unique_ptr<graphics::ArDepthImage> gArDepthImage = nullptr;
std::unique_ptr<graphics::TsdfVolume> gTsdfVolume = nullptr;
std::unique_ptr<graphics::MutableMesh> gWorldMesh = nullptr;
// Compute operations (own their pipelines, descriptor layouts, and GPU resources)
std::unique_ptr<graphics::TsdfFusionOp>        gTsdfFusionOp = nullptr;
std::unique_ptr<graphics::MarchingCubesOp>     gMarchingCubesOp = nullptr;
std::unique_ptr<reconstruction::ChiselManager> gChiselManager = nullptr;
std::unique_ptr<graphics::Texture2D> gMeshTexture = nullptr;
std::unique_ptr<graphics::Pipeline> gWorldMeshPipeline = nullptr;
std::unique_ptr<graphics::Renderable> gWorldMeshRenderable = nullptr;
extern "C" JNIEXPORT jstring JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

void UpdateARPlanes();
void DrawOffscreenRenderPass(VkCommandBuffer cmd, const uint32_t frameIndex);
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
    // Transparent Phong: UBO (binding 0, vert+frag) + texture sampler (binding 1, frag)
    auto transPhongDescriptorSetLayout = graphics::DescriptorSetLayoutBuilder(gVkContext->GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build();
    descriptorSetLayouts.insert({"transparent_phong", transPhongDescriptorSetLayout});
    auto transPhongPipelineLayout = graphics::PipelineLayoutBuilder(gVkContext->GetDevice())
            .AddDescriptorSetLayout(transPhongDescriptorSetLayout)
            .Build();
    pipelineLayouts.insert({"transparent_phong", transPhongPipelineLayout});
    // Opaque Phong: UBO only (binding 0, vert+frag) — no texture sampler
    auto opaquePhongDescriptorSetLayout = graphics::DescriptorSetLayoutBuilder(gVkContext->GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build();
    descriptorSetLayouts.insert({"opaque_phong", opaquePhongDescriptorSetLayout});
    auto opaquePhongPipelineLayout = graphics::PipelineLayoutBuilder(gVkContext->GetDevice())
            .AddDescriptorSetLayout(opaquePhongDescriptorSetLayout)
            .Build();
    pipelineLayouts.insert({"opaque_phong", opaquePhongPipelineLayout});
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
    // Compose: single texture sampler (binding 0, frag) for offscreen color image
    auto composeDescriptorSetLayout = graphics::DescriptorSetLayoutBuilder(gVkContext->GetDevice())
            .AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build();
    descriptorSetLayouts.insert({"compose", composeDescriptorSetLayout});
    auto composePipelineLayout = graphics::PipelineLayoutBuilder(gVkContext->GetDevice())
            .AddDescriptorSetLayout(composeDescriptorSetLayout)
            .Build();
    pipelineLayouts.insert({"compose", composePipelineLayout});
    // Compute operations are created below, after TsdfVolume and GpuMesh.
    // They own their own descriptor set layouts and pipeline layouts.

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
    // Load textures
    {
        std::vector<uint8_t> pixels;
        VkFormat fmt;
        int w, h;
        io::LoadImage("textures/grid.png", pixels, fmt, w, h);
        gGridTexture = std::make_unique<graphics::Texture2D>(
                gVkContext->GetDevice(),
                gVkContext->GetAllocator(),
                *gCommandPoolManager,
                pixels,
                static_cast<uint32_t>(w),
                static_cast<uint32_t>(h),
                fmt,
                "grid");
    }
    cameraBgQuad = std::make_unique<graphics::Renderable>("camera_bg");
    cameraBgQuad->SetMesh(gMeshes["fullscreen_quad"].get());
    composeQuad = std::make_unique<graphics::Renderable>("compose");
    composeQuad->SetMesh(gMeshes["fullscreen_quad"].get());
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
    //create the ar depth buffer object
    gArDepthImage = std::make_unique<graphics::ArDepthImage>(gVkContext->GetDevice(),
                                                             gVkContext->GetAllocator(),
                                                             "ArDepthImage");

    // Create CPU-uploaded mesh for OpenChisel output (replaces GpuMesh).
    gWorldMesh = std::make_unique<graphics::MutableMesh>(gVkContext->GetDevice(),
                                                          gVkContext->GetAllocator(),
                                                          *gCommandPoolManager,
                                                          WORLD_MESH_MAX_VERTICES,
                                                          WORLD_MESH_MAX_INDICES,
                                                          "WorldMesh");
    // OpenChisel-based reconstruction manager (worker thread handles integration)
    gChiselManager = std::make_unique<reconstruction::ChiselManager>();

    // ── Preserved compute infrastructure (not used for TSDF, kept for future use) ──
    gTsdfVolume = std::make_unique<graphics::TsdfVolume>(gVkContext->GetDevice(),
                                                          gVkContext->GetAllocator(),
                                                          *gCommandPoolManager,
                                                          "TsdfVolume");
    // Load mesh texture for the world mesh (transparent phong shading).
    // Uses textures/mesh.png if available; nullptr triggers a placeholder in the pipeline.
    if (io::AssetLoader::exists("textures/mesh.png")) {
        std::vector<uint8_t> pixels;
        VkFormat fmt;
        int w, h;
        io::LoadImage("textures/mesh.png", pixels, fmt, w, h);
        gMeshTexture = std::make_unique<graphics::Texture2D>(
                gVkContext->GetDevice(),
                gVkContext->GetAllocator(),
                *gCommandPoolManager,
                pixels,
                static_cast<uint32_t>(w),
                static_cast<uint32_t>(h),
                fmt,
                "mesh");
    } else {
        LOGI("textures/mesh.png not found — world mesh will use placeholder texture");
    }
    // Create a renderable for the world mesh (identity transform — mesh is already in world coords)
    gWorldMeshRenderable = std::make_unique<graphics::Renderable>("world_mesh");
    gWorldMeshRenderable->SetMesh(gWorldMesh.get());

    // ── Create compute operations ───────────────────────────────────────
    // Each operation owns its pipeline, descriptor layout, and GPU resources.
    // They are initialized lazily in nativeOnDrawFrame once depth dimensions
    // are known (deprojection needs image dimensions to allocate output buffers).
    graphics::ComputeOperation::InitContext computeCtx{
        gVkContext->GetDevice(), gVkContext->GetAllocator()
    };
    gTsdfFusionOp    = std::make_unique<graphics::TsdfFusionOp>(computeCtx);
    gMarchingCubesOp = std::make_unique<graphics::MarchingCubesOp>(computeCtx);

    // Wire up static connections (non-owning pointers to shared resources)
    gTsdfFusionOp->SetVolumeImageView(gTsdfVolume->GetImageView());
    gMarchingCubesOp->SetVolumeImageView(gTsdfVolume->GetImageView());
    // Note: gMarchingCubesOp output mesh not wired — world mesh now comes from OpenChisel
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
    gTransparentPhongPipeline = std::make_unique<graphics::Pipeline>(gOffscreenRenderPass.get(),
                                                                      gVkContext->GetDevice(),
                                                                      gVkContext->GetAllocator(),
                                                                      graphics::TransparentPhongConfig(gGridTexture.get(),
                                                                                                       gCommandPoolManager.get()),
                                                                      pipelineLayouts["transparent_phong"],
                                                                      descriptorSetLayouts["transparent_phong"]);
    // World mesh pipeline: opaque phong with fixed red color for TSDF mesh visualization
    gWorldMeshPipeline = std::make_unique<graphics::Pipeline>(gOffscreenRenderPass.get(),
                                                               gVkContext->GetDevice(),
                                                               gVkContext->GetAllocator(),
                                                               graphics::OpaquePhongConfig(glm::vec3(1.0f, 0.0f, 0.0f)),
                                                               pipelineLayouts["opaque_phong"],
                                                               descriptorSetLayouts["opaque_phong"]);
    gCameraBgPipeline = std::make_unique<graphics::Pipeline>(gSwapChainRenderPass.get(),
                                                              gVkContext->GetDevice(),
                                                              gVkContext->GetAllocator(),
                                                              graphics::CameraBackgroundConfig(
                                                                      gCameraImage.get(),
                                                                      &gDisplayRotation),
                                                              pipelineLayouts["camera_bg"],
                                                              descriptorSetLayouts["camera_bg"]);
    gComposePipeline = std::make_unique<graphics::Pipeline>(gSwapChainRenderPass.get(),
                                                             gVkContext->GetDevice(),
                                                             gVkContext->GetAllocator(),
                                                             graphics::ComposeConfig(gOffscreenRenderPass.get()),
                                                             pipelineLayouts["compose"],
                                                             descriptorSetLayouts["compose"]);
    gFrameSync->RecreateForSwapchain(gVkContext->getSwapchainImageCount());
}
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnSurfaceDestroyed(JNIEnv *env,
                                                                                      jobject thiz) {
    vkDeviceWaitIdle(gVkContext->GetDevice());
    gMeshes.clear();
}
int32_t previousArDepthWidth = 0;
uint32_t gFrameCount = 0;  // for throttling compute dispatches
extern "C"
JNIEXPORT void JNICALL
Java_dev_geronimodesenvolvimentos_krakatoa_VulkanSurfaceView_nativeOnDrawFrame(JNIEnv *env,
                                                                               jobject thiz) {

    gFrameSync->WaitForCurrentFrame();
    // Garbage-collect unused uniform buffers AFTER the fence wait guarantees
    // that command buffers from the oldest in-flight frame are done.
    // Must NOT run during command buffer recording (would destroy bound resources).
    if (gTransparentPhongPipeline) gTransparentPhongPipeline->CollectGarbage();
    if (gWorldMeshPipeline) gWorldMeshPipeline->CollectGarbage();
    if (gCameraBgPipeline) gCameraBgPipeline->CollectGarbage();
    if (gComposePipeline) gComposePipeline->CollectGarbage();

    gFrameTimer->Tick();
    gFrameSync->AdvanceFrame();
    VkSemaphore acquireSem = gFrameSync->GetNextAcquireSemaphore();
    gCommandPoolManager->AdvanceFrame();
    gCameraImage->AdvanceFrame();
    // Update ARCore first - acquires CPU camera image (YUV planes)
    m_eglDummy.makeCurrent();
    gArSessionManager->onDrawFrame();

    uint32_t imageIndex;
    vkAcquireNextImageKHR(gVkContext->GetDevice(), gVkContext->GetSwapchain(),
                          UINT64_MAX, acquireSem, VK_NULL_HANDLE, &imageIndex);

    gFrameSync->WaitForImage(imageIndex);
    gFrameSync->SetImageFence(imageIndex, gFrameSync->GetInFlightFence());
    gFrameSync->ResetCurrentFence();

    gCommandPoolManager->BeginFrame();
    VkCommandBuffer cmd = gCommandPoolManager->GetCurrentCommandBuffer();
    const uint32_t frameIndex = gVkContext->GetFrameIndex();
    /////////////////////////////
    // ── OpenChisel TSDF reconstruction ──────────────────────────────────
    // Depth data is sent to the ChiselManager worker thread for integration.
    // When new mesh data is available, it's uploaded to the MutableMesh.
    /////////////////////////////
    // Advance the world mesh ring buffer for this frame
    gWorldMesh->Advance();

    ArImage* depthImageHandle = gArSessionManager->getDepthImage();
    if (depthImageHandle != nullptr) {
        int32_t arDepthWidth = 0, arDepthHeight = 0;
        gArSessionManager->getDepthImageDimensions(depthImageHandle, arDepthWidth, arDepthHeight);

        // Lazy-init OpenChisel on first valid depth frame
        if (!gChiselManager->IsInitialized()) {
            assert(arDepthWidth > 0 && arDepthHeight > 0 && "Depth image has zero dimensions");
            gChiselManager->Initialize(
                0.01f,   // voxelResolution: 1cm voxels (each 16³ chunk = 16cm per side)
                0.03f,   // truncationDist:  3cm (≈3× voxel size)
                0.03f,   // carvingDist:     3cm
                true,    // enableCarving
                16,      // chunkSizeVoxels
                0        // threadCount: auto-detect
            );
            LOGI("[TSDF] Initialized ChiselManager on first depth frame (%dx%d)", arDepthWidth, arDepthHeight);
        }

        // Get depth data from ARCore
        int32_t depthStride = 0;
        std::vector<uint16_t> depthData{};
        gArSessionManager->getDepthImageData(depthImageHandle, depthData, depthStride);
        gArSessionManager->releaseDepthImage(depthImageHandle);

        // Diagnostic: check depth data quality
        static int depthFrameCount = 0;
        depthFrameCount++;
        if (depthFrameCount <= 5 || depthFrameCount % 60 == 0) {
            int nonZero = 0;
            uint16_t minVal = UINT16_MAX, maxVal = 0;
            for (uint16_t v : depthData) {
                if (v > 0) {
                    nonZero++;
                    if (v < minVal) minVal = v;
                    if (v > maxVal) maxVal = v;
                }
            }
            LOGI("[TSDF] depth frame %d: %dx%d, %d/%zu non-zero (%.1f%%), range %u-%u mm",
                 depthFrameCount, arDepthWidth, arDepthHeight,
                 nonZero, depthData.size(),
                 depthData.empty() ? 0.0 : 100.0 * nonZero / depthData.size(),
                 nonZero > 0 ? (unsigned)minVal : 0u,
                 nonZero > 0 ? (unsigned)maxVal : 0u);
        }

        // Get camera intrinsics scaled to depth resolution.
        // ArCamera_getImageIntrinsics returns values for the full-resolution CPU image
        // (e.g. 1920x1080), but the depth image is lower-res (e.g. 240x180).
        // Scale fx/fy/cx/cy by the ratio so they match the depth image pixel grid.
        ar::ArDepthIntrinsics arDepthIntrinsics{};
        gArSessionManager->getCameraIntrinsics(arDepthIntrinsics);
        float scaleX = static_cast<float>(arDepthWidth)  / static_cast<float>(arDepthIntrinsics.w);
        float scaleY = static_cast<float>(arDepthHeight) / static_cast<float>(arDepthIntrinsics.h);

        if (depthFrameCount <= 3) {
            LOGI("[TSDF] intrinsics: cam(%dx%d) fx=%.1f fy=%.1f cx=%.1f cy=%.1f → depth scale %.3f x %.3f → fx=%.1f fy=%.1f cx=%.1f cy=%.1f",
                 arDepthIntrinsics.w, arDepthIntrinsics.h,
                 arDepthIntrinsics.fx, arDepthIntrinsics.fy,
                 arDepthIntrinsics.cx, arDepthIntrinsics.cy,
                 scaleX, scaleY,
                 arDepthIntrinsics.fx * scaleX, arDepthIntrinsics.fy * scaleY,
                 arDepthIntrinsics.cx * scaleX, arDepthIntrinsics.cy * scaleY);
        }

        // Get the PHYSICAL (sensor-oriented) view matrix for TSDF integration.
        // ArCamera_getViewMatrix is display-oriented (rotated to match screen),
        // but the depth image and ArCamera_getImageIntrinsics are in the
        // unrotated sensor frame. Using the physical pose from ArCamera_getPose
        // ensures the view matrix matches the depth data coordinate system.
        std::array<float, 16> arViewMatrix{};
        gArSessionManager->getSensorViewMatrix(arViewMatrix.data());

        // Queue frame for async integration (non-blocking)
        reconstruction::ChiselManager::FrameInput frameInput;
        frameInput.depthData = std::move(depthData);
        frameInput.width = arDepthWidth;
        frameInput.height = arDepthHeight;
        frameInput.fx = arDepthIntrinsics.fx * scaleX;
        frameInput.fy = arDepthIntrinsics.fy * scaleY;
        frameInput.cx = arDepthIntrinsics.cx * scaleX;
        frameInput.cy = arDepthIntrinsics.cy * scaleY;
        frameInput.viewMatrix = arViewMatrix;
        gChiselManager->IntegrateFrame(frameInput);
    } else {
        static int depthNullCount = 0;
        depthNullCount++;
        if (depthNullCount <= 3 || depthNullCount % 120 == 0) {
            LOGI("[TSDF] getDepthImage returned nullptr (%d times so far)", depthNullCount);
        }
    }

    // Check if worker thread produced a new mesh — upload to GPU if so
    reconstruction::ChiselManager::MeshOutput meshOut;
    if (gChiselManager && gChiselManager->PollMesh(meshOut) && !meshOut.indices.empty()) {
        uint32_t vertCount = static_cast<uint32_t>(meshOut.vertices.size() / 8);
        uint32_t idxCount = static_cast<uint32_t>(meshOut.indices.size());
        gWorldMesh->UpdateMesh(meshOut.vertices.data(), vertCount,
                               meshOut.indices.data(), idxCount);
        static int meshUploadCount = 0;
        meshUploadCount++;
        if (meshUploadCount <= 5 || meshUploadCount % 60 == 0) {
            LOGI("[TSDF] mesh upload #%d: %u verts, %u indices", meshUploadCount, vertCount, idxCount);
        }
    }

    ////////////////////////////
    // Update AR planes
    UpdateARPlanes();

    // Upload camera feed (YUV->RGBA) into the ring-buffered Vulkan image.
    // After this call the current image is in SHADER_READ_ONLY_OPTIMAL, ready to sample.
    gCameraImage->Update(cmd, gArSessionManager->getCameraFrame());
    //The offscreen render pass draws the planes
    DrawOffscreenRenderPass(cmd, frameIndex);

    //begin the swap chain render pass
    gSwapChainRenderPass->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gSwapChainRenderPass->Begin(cmd,
                                gSwapChainRenderPass->GetFramebuffer(imageIndex),
                                gVkContext->getSwapchainExtent());
    // Draw camera background (fullscreen quad with camera texture, depth=1.0)
    if (gCameraImage->IsValid()) {
        gCameraBgPipeline->Bind(cmd);
        gCameraBgPipeline->Draw(cmd, nullptr, cameraBgQuad.get(), frameIndex);
    }
    // Composite offscreen render target (AR planes) over the camera background
    gComposePipeline->Bind(cmd);
    gComposePipeline->Draw(cmd, nullptr, composeQuad.get(), frameIndex);
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
    // Destroy compute operations before their shared resources (TsdfVolume, GpuMesh)
    gMarchingCubesOp = nullptr;
    gTsdfFusionOp    = nullptr;
    gWorldMesh = nullptr;
    gTsdfVolume = nullptr;
    gArDepthImage = nullptr;
    gWorldMeshRenderable = nullptr;
    gComposePipeline = nullptr;
    gCameraBgPipeline = nullptr;
    gWorldMeshPipeline = nullptr;
    gTransparentPhongPipeline = nullptr;
    gUnshadedOpaquePipeline = nullptr;
    gMeshTexture = nullptr;
    gGridTexture = nullptr;
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
    if (gArSessionManager)
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

void UpdateARPlanes() {
    for(auto p:gArPlanes){
        //std::unordered_map<int64_t, std::shared_ptr<graphics::Renderable>> gArPlanes;
        ((graphics::MutableMesh*)p.second->GetMesh())->Advance();
    }
    gArSessionManager->forEachPlane([&](int64_t planeid, const float* modelMat,
                                        const float* polygon, int polyFloatCount){
        // Generate the mesh from the polygon contour (centroid fan)
        auto meshData = io::GenerateARPlaneMesh(polygon, polyFloatCount, 1.0f);
        // nothing, leave this functions
        if (meshData->indices.empty())
            return;
        assert(meshData->indexCount > 0);
        assert(meshData->vertexCount > 0);
        //TODO: Seek renderables by plane id
        auto itPlanes = gArPlanes.find(planeid);
        if(itPlanes == gArPlanes.end()) {
            //no plane with this id, create a new renderable, with a new mutable mesh and add to the plane.
            auto name = Concatenate("AR_PLANE ", planeid);
            std::shared_ptr<graphics::Renderable> newRenderable = std::make_shared<graphics::Renderable>(planeid);
            graphics::MutableMesh* newMesh = new graphics::MutableMesh(gVkContext->GetDevice(),
                                                                       gVkContext->GetAllocator(),
                                                                       *(gCommandPoolManager.get()),
                                                                       graphics::MutableMesh::DEFAULT_MAX_VERTS,
                                                                       graphics::MutableMesh::DEFAULT_MAX_INDICES,
                                                                       name);
            newRenderable->SetMesh(newMesh, true);
            gArPlanes.insert({planeid, newRenderable});
            newMesh->Advance();
        }
        auto planeRenderable = gArPlanes[planeid];
        //TODO: update the mutable mesh
        auto mutableMesh = reinterpret_cast<graphics::MutableMesh*>(planeRenderable->GetMesh());
        mutableMesh->UpdateMesh(meshData->vertices.data(), meshData->vertexCount, meshData->indices.data(), meshData->indexCount);
        //TODO: update the model transform of the renderable
        planeRenderable->GetTransform().SetFromMatrixPtr(modelMat);
        auto msg = Concatenate("[arplanes] updated plane ", planeid);
        LOGI("%s", msg.c_str());
        //Unlike the original function i wrote the dra w is decoupled from the assembly
        //and data gathering phases, so the drawing will happen later, when i have render passes
        //and pipelines
    });
}

void DrawOffscreenRenderPass(VkCommandBuffer cmd, const uint32_t frameIndex){
    //begin the offscreen render pass
    gOffscreenRenderPass->setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gOffscreenRenderPass->AdvanceFrame();
    gOffscreenRenderPass->Begin(cmd, gOffscreenRenderPass->GetFramebuffer(), gOffscreenRenderPass->GetExtent());
    // Gather AR light estimation for Phong shading
    const auto& lightEst = gArSessionManager->getLightEstimate();
    glm::vec4 lightDir(0.0f, -1.0f, -0.5f, 0.0f);
    float intensity = lightEst.valid ? lightEst.pixelIntensity : 1.0f;
    glm::vec4 lightColor(
            lightEst.valid ? lightEst.colorCorrection[0] : 1.0f,
            lightEst.valid ? lightEst.colorCorrection[1] : 1.0f,
            lightEst.valid ? lightEst.colorCorrection[2] : 1.0f,
            intensity);
    glm::vec4 ambientColor(0.3f * intensity, 0.3f * intensity, 0.3f * intensity, 1.0f);

    // Draw AR planes into the offscreen render target
    for (const auto& plane : gArPlanes)
    {
        graphics::RDO rdo;
        rdo.Add(graphics::RDO::Keys::MODEL_MAT, plane.second->GetTransform().GetWorldMatrix());

        std::array<float,16> arViewMatrix{};
        gArSessionManager->getViewMatrix(arViewMatrix.data());
        glm::mat4 viewMat = glm::make_mat4(arViewMatrix.data());
        rdo.Add(graphics::RDO::Keys::VIEW_MAT, viewMat);

        std::array<float,16> arProjMatrix{};
        gArSessionManager->getProjectionMatrix(0.01f, 100.f, arProjMatrix.data());
        glm::mat4 projMat = glm::make_mat4(arProjMatrix.data());
        rdo.Add(graphics::RDO::Keys::PROJ_MAT, projMat);

        rdo.Add(graphics::RDO::Keys::LIGHT_DIR, lightDir);
        rdo.Add(graphics::RDO::Keys::LIGHT_COLOR, lightColor);
        rdo.Add(graphics::RDO::Keys::AMBIENT_COLOR, ambientColor);

        gTransparentPhongPipeline->Bind(cmd);
        gTransparentPhongPipeline->Draw(cmd, &rdo, plane.second.get(), frameIndex);
        auto msg = Concatenate("[arplanes] drew plane ", plane.second->GetId());
        LOGI("%s", msg.c_str());
    }
    // Draw the reconstructed world mesh (marching cubes output)
    // Note: index count is checked GPU-side via indirect draw, not CPU-side
    if (gWorldMeshPipeline && gWorldMeshRenderable && gWorldMesh) {
        graphics::RDO rdo;
        // Identity model matrix — mesh is already in world coordinates
        rdo.Add(graphics::RDO::Keys::MODEL_MAT, glm::mat4(1.0f));

        std::array<float,16> arViewMatrix{};
        gArSessionManager->getViewMatrix(arViewMatrix.data());
        glm::mat4 viewMat = glm::make_mat4(arViewMatrix.data());
        rdo.Add(graphics::RDO::Keys::VIEW_MAT, viewMat);

        std::array<float,16> arProjMatrix{};
        gArSessionManager->getProjectionMatrix(0.01f, 100.f, arProjMatrix.data());
        glm::mat4 projMat = glm::make_mat4(arProjMatrix.data());
        rdo.Add(graphics::RDO::Keys::PROJ_MAT, projMat);

        rdo.Add(graphics::RDO::Keys::LIGHT_DIR, lightDir);
        rdo.Add(graphics::RDO::Keys::LIGHT_COLOR, lightColor);
        rdo.Add(graphics::RDO::Keys::AMBIENT_COLOR, ambientColor);

        gWorldMeshPipeline->Bind(cmd);
        gWorldMeshPipeline->Draw(cmd, &rdo, gWorldMeshRenderable.get(), frameIndex);
    }
    gOffscreenRenderPass->End(cmd);
}
