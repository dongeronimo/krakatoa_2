# Compute Operations Architecture

This document describes the GPU compute pipeline that turns ARCore depth frames
into a triangle mesh in real time. It also explains how to add new compute
operations to the chain.

---

## Overview

The system is a linear pipeline of three compute shader stages:

```
ARCore Depth (uint16[])
        │
        ▼
┌───────────────────────┐
│  DepthDeprojectionOp  │   depth pixels → world-space vec4 positions
└───────────┬───────────┘
            │  position SSBO (ring-buffered)
            ▼
┌───────────────────────┐
│    VoxelizationOp     │   positions → 3D occupancy volume (256³ R8_UINT)
└───────────┬───────────┘
            │  shared VoxelVolume image
            ▼
┌───────────────────────┐
│    MarchingCubesOp    │   volume → triangle mesh (GpuMesh)
└───────────┬───────────┘
            │  vertex + index SSBOs → indirect draw
            ▼
       Render pass
```

Each stage is a subclass of `ComputeOperation`. The base class owns the
descriptor set layout, pipeline layout, and `ComputePipeline` wrapper. Subclasses
own their persistent GPU resources (lookup tables, ring buffers, etc.).

---

## Key Classes

### `ComputeOperation` (base class)

**File:** `compute_operation.h / .cpp`

Abstract base with a simple lifecycle:

| Method | Purpose |
|---|---|
| `Initialize()` | One-time setup: create layouts, pipeline, GPU resources. |
| `Execute(cmd, frameIndex)` | Per-frame: update descriptors, push constants, dispatch. |
| `InsertPostBarrier(cmd)` | Memory barrier between this stage and the next. |
| `GetOutputBuffer(frameIndex)` | Expose output buffer generically to downstream ops. |
| `GetOutputImageView()` | Expose output image generically to downstream ops. |

Protected helpers for subclasses:
- `CreateDescriptorSetLayout(bindings)` — creates a `VkDescriptorSetLayout`.
- `CreatePipelineLayout(dsLayout, pushConstant*)` — creates a `VkPipelineLayout`.

### `ComputePipeline` (Vulkan wrapper)

**File:** `compute_pipeline.h / .cpp`

Thin wrapper that owns the `VkPipeline`, descriptor pool, and ring-buffered
descriptor sets. Does **not** own layouts (those belong to the operation).

```cpp
struct ComputePipelineConfig {
    std::string shaderName;                        // "foo" → loads shaders/foo.comp.spv
    std::vector<VkDescriptorPoolSize> poolSizes;   // descriptor types and counts
};
```

Key methods: `Bind(cmd)`, `GetDescriptorSet(frameIndex)`, `DispatchRaw(cmd, x, y, z)`.

---

## Existing Operations

### 1. DepthDeprojectionOp

**Shader:** `deprojection.comp`

Converts a uint16 depth buffer into world-space `vec4` positions using camera
intrinsics and the view-inverse matrix.

| Binding | Type | Content |
|---------|------|---------|
| 0 | SSBO | Camera intrinsics (fx, fy, cx, cy) |
| 1 | SSBO | Depth input (uint16[]) |
| 2 | SSBO | Output positions (vec4[]) |

**Push constants:** `viewInverse` (mat4), `width`, `height`

**Dispatch:** 2D — `ceil(width/16) × ceil(height/16) × 1`

**Barrier override:** Allows both compute-read and vertex-read downstream.

**Ring-buffered resources:** output position buffers, intrinsics SSBOs (host-visible, persistently mapped).

### 2. VoxelizationOp

**Shader:** `voxelization.comp`

Reads world-space positions and accumulates them into a shared 3D volume image
(`R8_UINT`, 256³). Each position maps to a voxel cell and increments its value.

| Binding | Type | Content |
|---------|------|---------|
| 0 | SSBO | Positions (from upstream op) |
| 1 | Storage Image | 3D volume (r8ui) |

**Push constants:** `positionCount`, `scale`, `volumeSize`

**Dispatch:** 1D — `ceil(positionCount/256) × 1 × 1`

**Upstream wiring:** generic — calls `positionSource->GetOutputBuffer(frameIndex)`.

### 3. MarchingCubesOp

**Shader:** `marching_cubes.comp`

Reads the voxel volume, evaluates the marching cubes algorithm per cell, and
writes vertices/indices into a `GpuMesh`. Uses `atomicAdd` on a counter buffer
to append without CPU readback.

| Binding | Type | Content |
|---------|------|---------|
| 0 | Storage Image | 3D volume (r8ui) |
| 1 | SSBO | Edge table (256 ints, immutable) |
| 2 | SSBO | Triangle table (256×16 ints, immutable) |
| 3 | SSBO | Vertex output (writes to GpuMesh) |
| 4 | SSBO | Index output (writes to GpuMesh) |
| 5 | SSBO | Atomic counters [vertexCount, indexCount] |

**Push constants:** `cutoff`, `scale`, `maxDistance`, `volumeSize`, `maxVertices`, `maxIndices`

**Dispatch:** 3D — `ceil(255/4) × ceil(255/4) × ceil(255/4)`

**Barrier override:** Allows vertex-input-read and transfer-read (for indirect draw copy).

After dispatch, `GpuMesh::PrepareIndirectDraw(cmd)` copies the atomic counter
into a `VkDrawIndexedIndirectCommand` so the mesh can be drawn without any CPU
readback of vertex/index counts.

---

## Per-Frame Execution Order

Orchestrated in `native-lib.cpp`:

```
1. Acquire depth frame from ARCore
2. Upload depth to ring-buffered SSBO (ArDepthImage::UpdateImage)
3. DepthDeprojectionOp::Execute()
4. DepthDeprojectionOp::InsertPostBarrier()
5. VoxelizationOp::Execute()
6. VoxelizationOp::InsertPostBarrier()
7. GpuMesh::ResetCounters()          ← zero vertex/index counts
8. MarchingCubesOp::Execute()
9. MarchingCubesOp::InsertPostBarrier()
10. GpuMesh::PrepareIndirectDraw()    ← counter → indirect draw buffer
11. Begin render pass, draw mesh with vkCmdDrawIndexedIndirect
```

---

## Ring Buffering

All per-frame resources are triple-buffered (`MAX_FRAMES_IN_FLIGHT = 3`) using
the `RingBuffer<T>` template from `ring_buffer.h`. This lets frames overlap on
the GPU without synchronization hazards.

Ring-buffered resources include:
- Depth input SSBOs (`ArDepthImage`)
- Deprojection output position SSBOs
- Intrinsics SSBOs
- Descriptor sets (one per operation per frame)

Shared resources like the `VoxelVolume` image and `GpuMesh` buffers are **not**
ring-buffered — they are written and read within the same frame's command buffer,
synchronized by barriers.

---

## How to Add a New Compute Operation

### Step 1: Write the compute shader

Create `app/src/main/shaders/my_op.comp`. Declare your bindings and push
constants, write a `main()` that does the work.

### Step 2: Create the header

```cpp
// my_op.h
#include "compute_operation.h"

namespace graphics {

class MyOp : public ComputeOperation {
public:
    explicit MyOp(const InitContext& ctx);
    ~MyOp() override;

    void Initialize() override;
    void Execute(VkCommandBuffer cmd, uint32_t frameIndex) override;

    // Override if your output feeds non-compute stages:
    // void InsertPostBarrier(VkCommandBuffer cmd) override;

    // Expose output to downstream ops:
    // VkBuffer GetOutputBuffer(uint32_t frameIndex) const override;

    // Setters for wiring and per-frame params...
    void SetSomeInput(ComputeOperation* upstream);
    void SetSomeParam(float value);

private:
    struct PushConstant { /* must match shader */ };

    ComputeOperation* upstream_ = nullptr;
    float param_ = 0.0f;

    // Owned GPU resources...
};

} // namespace graphics
```

### Step 3: Implement Initialize()

In `Initialize()`, create your descriptor set layout, pipeline layout, compute
pipeline, and any persistent GPU resources (lookup tables, output buffers).

```cpp
void MyOp::Initialize() {
    // 1. Descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    descriptorSetLayout = CreateDescriptorSetLayout(bindings);

    // 2. Pipeline layout (with optional push constants)
    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstant)};
    pipelineLayout = CreatePipelineLayout(descriptorSetLayout, &pcRange);

    // 3. Compute pipeline
    ComputePipelineConfig config;
    config.shaderName = "my_op";  // → shaders/my_op.comp.spv
    config.descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * MAX_FRAMES_IN_FLIGHT},
    };
    pipeline = std::make_unique<ComputePipeline>(
        device, allocator, config, pipelineLayout, descriptorSetLayout);

    // 4. Create any owned GPU resources here...

    initialized = true;
}
```

### Step 4: Implement Execute()

Update descriptors, push constants, bind, and dispatch.

```cpp
void MyOp::Execute(VkCommandBuffer cmd, uint32_t frameIndex) {
    // 1. Get the descriptor set for this frame
    VkDescriptorSet ds = pipeline->GetDescriptorSet(frameIndex);

    // 2. Write descriptors (input from upstream, your output, etc.)
    VkBuffer inputBuf = upstream_->GetOutputBuffer(frameIndex);
    VkDescriptorBufferInfo inputInfo{inputBuf, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo outputInfo{outputBuffer, 0, VK_WHOLE_SIZE};

    VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &inputInfo, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputInfo, nullptr},
    };
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    // 3. Bind pipeline and descriptor set
    pipeline->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &ds, 0, nullptr);

    // 4. Push constants
    PushConstant pc{/* ... */};
    vkCmdPushConstants(cmd, pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    // 5. Dispatch
    pipeline->DispatchRaw(cmd, groupCountX, groupCountY, groupCountZ);
}
```

### Step 5: Override InsertPostBarrier() if needed

The default barrier is compute-write → compute-read. Override if your output
feeds the vertex shader, transfer stage, or fragment shader:

```cpp
void MyOp::InsertPostBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}
```

### Step 6: Wire it up in native-lib.cpp

```cpp
// Creation
gMyOp = std::make_unique<graphics::MyOp>(initCtx);
gMyOp->SetSomeInput(gPreviousOp.get());

// Initialization (with the others)
gMyOp->Initialize();

// Per-frame execution (in the compute section of the command buffer)
gMyOp->SetSomeParam(value);
gMyOp->Execute(cmd, frameIndex);
gMyOp->InsertPostBarrier(cmd);
```

### Step 7: Compile the shader

Place the GLSL source in `app/src/main/shaders/my_op.comp`. The build system
compiles `*.comp` files to SPIR-V (`*.comp.spv`) and bundles them as Android
assets under `shaders/`.

---

## Design Conventions

- **Generic upstream linking:** Use `ComputeOperation*` and `GetOutputBuffer()`
  instead of concrete types. This keeps operations loosely coupled.
- **Push constants for per-frame data, SSBOs for bulk data.** Push constants are
  limited to 128 bytes on many mobile GPUs.
- **Ring-buffer any resource written by the CPU per frame** to avoid stalling on
  in-flight frames.
- **Shared resources (VoxelVolume, GpuMesh) are synchronized by barriers**, not
  ring-buffered, because they're written and read within the same command buffer.
- **Lazy initialization:** Operations are constructed and wired first, then
  `Initialize()` is called once runtime parameters (like depth dimensions) are
  known.
- **Operations own their layouts; ComputePipeline does not.** This avoids
  double-free and makes ownership unambiguous.
