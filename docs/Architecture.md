# Krakatoa Architecture

Krakatoa is an Android AR application that performs real-time 3D reconstruction using ARCore depth data and a TSDF (Truncated Signed Distance Field) volume. It renders the result as a mesh overlay on the camera feed using Vulkan.

## High-Level Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Android (Kotlin)                        │
│  MainActivity ──> VulkanSurfaceView ──> JNI native calls    │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                      Native C++ Layer                       │
│                                                             │
│  ┌──────────┐   ┌──────────────┐   ┌────────────────────┐  │
│  │  ARCore   │   │   Vulkan     │   │  Reconstruction    │  │
│  │  Manager  │   │   Renderer   │   │  (OpenChisel)      │  │
│  └─────┬─────┘   └──────┬──────┘   └────────┬───────────┘  │
│        │                 │                    │              │
│   depth, pose,      pipelines,          TSDF integration,   │
│   camera frames     render passes       mesh extraction     │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## Android Layer

### `MainActivity.kt`
- Entry point. Checks camera permissions and ARCore availability.
- Creates `VulkanSurfaceView` and adds it to the layout.

### `VulkanSurfaceView.kt`
- Custom `SurfaceView` that manages a dedicated render thread.
- Implements `SurfaceHolder.Callback` for surface lifecycle.
- Forwards touch events and surface changes to native code via JNI.
- Loads `libkrakatoad.so` (the native library).

## Native Layer

### ARCore Integration (`ar_manager.h/cpp`)

`ARSessionManager` wraps the ARCore C API (dynamically loaded via `ar_loader.h`):

| Method | Purpose |
|--------|---------|
| `initialize()` | Creates AR session, configures depth mode |
| `onDrawFrame()` | Updates AR frame, acquires camera image and tracking state |
| `getDepthImage()` | Acquires the current depth frame (uint16, millimeters) |
| `getCameraIntrinsics()` | Returns fx, fy, cx, cy, width, height (sensor coordinates) |
| `getViewMatrix()` | Display-oriented view matrix (for rendering) |
| `getSensorViewMatrix()` | Physical sensor-oriented view matrix (for TSDF integration) |
| `getProjectionMatrix()` | Display-aware projection matrix (accounts for rotation) |
| `forEachPlane()` | Iterates detected AR planes |

**Key insight**: `getViewMatrix()` and `getSensorViewMatrix()` serve different purposes:
- `getViewMatrix()` → for **rendering** (axes match the screen)
- `getSensorViewMatrix()` → for **TSDF integration** (axes match the depth image)

### Vulkan Rendering

#### Context (`vk_context.h/cpp`)
- Vulkan instance, device, swapchain, VMA allocator
- Multiple queue families: graphics, compute, transfer, present
- Frame index management for ring buffering (3 frames in flight)

#### Render Passes
- **OffscreenRenderPass**: Renders 3D content (planes, world mesh) to an offscreen target
- **SwapchainRenderPass**: Composites offscreen result over camera background to the screen

#### Pipelines (`pipeline.h/cpp`)
| Pipeline | Shader | Purpose |
|----------|--------|---------|
| CameraBackground | `camera_bg` | Converts NV12 YUV camera feed to RGB |
| OpaquePhong | `opaque_phong` | Renders world mesh (fixed red color + Phong lighting) |
| TransparentPhong | `transparent_phong` | Renders AR planes with grid texture |
| Compose | `compose` | Fullscreen quad compositing offscreen → swapchain |

#### Mesh Types
- **StaticMesh**: Immutable GPU buffers (loaded from glTF assets)
- **MutableMesh**: Ring-buffered CPU→GPU streaming mesh (used for reconstruction output)
- **GpuMesh**: Compute shader-generated mesh with atomic counters (GPU compute path, kept for reference)

### TSDF Reconstruction (`chisel_bridge/chisel_manager.h/cpp`)

The active reconstruction path uses OpenChisel on a dedicated worker thread.
The chisel bridge lives in its own subdirectory (`chisel_bridge/`) with a
dedicated CMakeLists.txt compiled at -O3 with aggressive optimizations
(-ffast-math, -funroll-loops, -ftree-vectorize, -ffp-contract=fast,
-fomit-frame-pointer). This is critical because OpenChisel's
ProjectionIntegrator::Integrate is a header template — at -O0 the Eigen
inner loop is 50-100x slower.

```
Render Thread                          Worker Thread
─────────────                          ─────────────

IntegrateFrame(depth, intrinsics,      WorkerLoop():
               viewMatrix)             │
  │                                    ├─ Wait for signal
  ├─ Store in latest-wins slot         ├─ Grab latest FrameInput
  └─ Signal worker via condvar         ├─ ProcessFrame():
                                       │   ├─ uint16 mm → float meters
                                       │   ├─ Build PinholeCamera
PollMesh():                            │   ├─ Convert pose (GL→CV)
  │                                    │   ├─ Chisel::IntegrateDepthScan()
  ├─ Drain event queue                 │   ├─ PruneDistantChunks()
  ├─ If EVT_MESH_READY:               │   ├─ Chisel::UpdateMeshes()
  │   └─ Deserialize vertex/index      │   └─ ConsolidateChunkMeshes()
  │       data from payload            │       └─ Post EVT_MESH_READY
  └─ Upload to MutableMesh             │
                                       └─ Loop
```

**Latest-wins design**: If the worker is still busy when a new frame arrives, the old queued frame is silently overwritten. This prevents queue buildup and keeps the reconstruction responsive.

**Chunk pruning**: When chunk count exceeds `MAX_CHUNKS` (8000), the farthest chunks from the camera are removed to bound memory usage.

**Known limitation — OOM on room-scale scans**: The current architecture sends
ALL chunk meshes to the GPU every mesh frame via `ConsolidateChunkMeshes()`.
At 1cm voxels, scanning a full room quickly exhausts both CPU RAM (OpenChisel
stores all chunks in memory) and GPU memory (single consolidated buffer grows
unbounded). See `docs/ChunkPagingProposal.md` for the planned fix.

### GPU Compute Path (Reference)

The codebase also contains a GPU compute pipeline for reconstruction (currently unused in favor of OpenChisel):

| Stage | Shader | Purpose |
|-------|--------|---------|
| DepthDeprojectionOp | `deprojection.comp` | Depth pixels → world-space positions |
| VoxelizationOp | `voxelization.comp` | Positions → 3D occupancy grid (256^3) |
| MarchingCubesOp | `marching_cubes.comp` | Occupancy grid → triangle mesh |
| TsdfFusionOp | `tsdf_fusion.comp` | Depth → TSDF volume (128^3, packed distance+weight) |

These are kept for future reference/experimentation.

## Frame Loop (`native-lib.cpp`)

Each frame in `nativeOnDrawFrame()`:

1. **Advance** ring buffers (mesh, command pools)
2. **Acquire** depth image from ARCore
3. **Scale intrinsics** from camera image resolution to depth resolution
4. **Get sensor view matrix** (physical camera pose, not display-oriented)
5. **Queue frame** for async TSDF integration (`ChiselManager::IntegrateFrame`)
6. **Poll mesh** from worker thread (`ChiselManager::PollMesh`)
7. **Upload mesh** to `MutableMesh` if new data available
8. **Render**:
   - Camera background (NV12 → RGB)
   - Begin offscreen pass
   - Draw AR planes (transparent Phong)
   - Draw world mesh (opaque Phong, red)
   - End offscreen pass
   - Compose to swapchain
9. **Present**

## Build System

- **Gradle** (`app/build.gradle.kts`): Android build, min API 33, arm64-v8a only
- **CMake** (`app/src/main/cpp/CMakeLists.txt`): C++17, fetches dependencies via FetchContent
- **chisel_bridge** (`app/src/main/cpp/chisel_bridge/CMakeLists.txt`): Separate static lib, -O3 always
- **Dependencies**: GLM, nlohmann/json, Assimp, Eigen, OpenChisel, VMA
- **Shader compilation**: GLSL → SPIR-V via `glslc` (invoked from CMake)

## Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| MAX_FRAMES_IN_FLIGHT | 3 | ring_buffer.h |
| Voxel resolution | 1cm (0.01m) | native-lib.cpp (Initialize call) |
| Truncation distance | 3cm (0.03m) | native-lib.cpp (Initialize call) |
| Carving distance | 3cm (0.03m) | native-lib.cpp (Initialize call) |
| Chunk size | 16x16x16 voxels | native-lib.cpp (Initialize call) |
| Max chunks | 8000 | chisel_bridge/chisel_manager.h |
| Max mesh vertices | 2,000,000 | CMakeLists.txt (WORLD_MESH_MAX_VERTICES) |
| Max mesh indices | 6,000,000 | CMakeLists.txt (WORLD_MESH_MAX_INDICES) |
| Depth far plane | 1.5m | chisel_bridge/chisel_manager.cpp |
