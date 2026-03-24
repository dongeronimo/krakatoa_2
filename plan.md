# Refactor: ComputeOperation Architecture

## Goal
Replace the current `ComputePipeline` + `ComputePipelineConfig` + `CDO` system with self-contained `ComputeOperation` subclasses that own their resources.

## Design

### Base Class: `ComputeOperation`

```cpp
// compute_operation.h
class ComputeOperation {
public:
    ComputeOperation(VkDevice device, VmaAllocator allocator, const std::string& shaderName);
    virtual ~ComputeOperation();

    // Called once when dimensions/dependencies are known. Creates pipeline, descriptor layout,
    // pipeline layout, descriptor pool, ring-buffered descriptor sets, and owned buffers.
    void Build();

    // Record commands: bind pipeline, update descriptors, push constants, dispatch.
    void RecordCommands(VkCommandBuffer cmd, uint32_t frameIndex);

    // Insert the post-dispatch memory barrier.
    void RecordBarrier(VkCommandBuffer cmd);

protected:
    // Subclasses implement these:
    virtual VkDescriptorSetLayout CreateDescriptorSetLayout() = 0;
    virtual VkPipelineLayout CreatePipelineLayout(VkDescriptorSetLayout dsl) = 0;
    virtual std::vector<VkDescriptorPoolSize> GetDescriptorPoolSizes() = 0;
    virtual void CreateResources() = 0;  // Allocate owned ring-buffered buffers
    virtual void UpdateDescriptorsAndDispatch(VkCommandBuffer cmd, uint32_t frameIndex) = 0;
    virtual VkMemoryBarrier GetPostBarrier() = 0;
    virtual VkPipelineStageFlags GetPostBarrierDstStage() = 0;

    VkDevice device;
    VmaAllocator allocator;
    std::string shaderName;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    RingBuffer<VkDescriptorSet> descriptorSets;
    bool built = false;
};
```

### Concrete Subclasses

#### `DepthDeprojectionOp`
**Owns:** intrinsics UBO ring buffer (3x), output position SSBO ring buffer (3x)
**External inputs (setters):**
- `SetDepthInput(VkBuffer depthBuffer, uint32_t width, uint32_t height)`
- `SetCameraParams(float fx, float fy, float cx, float cy, const std::array<float,16>& viewInverse)`

**Output accessor:** `VkBuffer GetOutputBuffer(uint32_t frameIndex)` or `GetCurrentOutputBuffer()`
**Barrier:** SHADER_WRITE → SHADER_READ | VERTEX_SHADER (COMPUTE → COMPUTE | VERTEX)

#### `VoxelizationOp`
**Owns:** nothing extra (references external resources)
**External inputs (setters):**
- `SetPositionsBuffer(VkBuffer buf, uint32_t positionCount)`
- `SetVolumeImageView(VkImageView view)`
- `SetScale(float scale)`

**Barrier:** SHADER_WRITE → SHADER_READ (COMPUTE → COMPUTE)

#### `MarchingCubesOp`
**Owns:** edge table SSBO, triangle table SSBO (created once in CreateResources)
**External inputs (setters):**
- `SetVolumeImageView(VkImageView view)`
- `SetMesh(GpuMesh& mesh)` — gets vertex/index/counter buffers from it
- `SetParams(uint32_t cutoff, float scale, float maxDistance)`

**Barrier:** SHADER_WRITE → VERTEX_ATTRIBUTE_READ | INDEX_READ | TRANSFER_READ (COMPUTE → VERTEX_INPUT | TRANSFER)

## Implementation Steps

### Step 1: Create `compute_operation.h` and `compute_operation.cpp`
- Base class with Build(), RecordCommands(), RecordBarrier()
- Build() calls the virtual methods to create layout, pool, sets, resources
- RecordCommands() binds pipeline, calls UpdateDescriptorsAndDispatch()
- RecordBarrier() calls GetPostBarrier()/GetPostBarrierDstStage()
- Shader loading reused from current compute_pipeline.cpp

### Step 2: Create `depth_deprojection_op.h` and `depth_deprojection_op.cpp`
- Absorbs: depth_deprojection_config.cpp + DepthDeprojectionOutput struct
- Owns ring-buffered intrinsics UBOs and output position SSBOs
- Constructor takes (device, allocator) only
- Build(width, height) creates output buffers sized to depth dimensions
- Setters for per-frame data (depth buffer, intrinsics, view inverse)

### Step 3: Create `voxelization_op.h` and `voxelization_op.cpp`
- Absorbs: voxelization_config.cpp
- No owned buffers — all inputs come from setters
- Setters: positions buffer, volume image view, scale, volume size

### Step 4: Create `marching_cubes_op.h` and `marching_cubes_op.cpp`
- Absorbs: marching_cubes_config.cpp
- Owns edge table and triangle table SSBOs (created once)
- Setters for volume view, GpuMesh reference, tuning params

### Step 5: Refactor `native-lib.cpp`
- Replace `ComputePipeline` + `CDO` usage with new `ComputeOperation` subclasses
- Remove: `gDeprojectionPipeline`, `gDepthDeprojectionOutput`, `gVoxelizationPipeline`, `gMarchingCubesPipeline`
- Replace with: `gDeprojectionOp`, `gVoxelizationOp`, `gMarchingCubesOp`
- Pipeline layout/descriptor set layout creation for compute moves INTO the operations
- Remove `pipelineLayouts["compute_*"]` and `descriptorSetLayouts["compute_*"]` entries
- Remove CDO construction — use direct setters instead
- Barriers move into RecordBarrier() calls

**native-lib.cpp before:**
```cpp
graphics::CDO deprojectCDO;
deprojectCDO.Add(CDO::Keys::fx, ...);
// ... 8 more adds
gDeprojectionPipeline->Dispatch(cmd, frameIndex, deprojectCDO);
// manual barrier
```

**native-lib.cpp after:**
```cpp
gDeprojectionOp->SetDepthInput(currentDepthBuffer, arDepthWidth, arDepthHeight);
gDeprojectionOp->SetCameraParams(fx, fy, cx, cy, viewInvArray);
gDeprojectionOp->RecordCommands(cmd, frameIndex);
gDeprojectionOp->RecordBarrier(cmd);
```

### Step 6: Update CMakeLists.txt
- Remove: compute_pipeline.cpp/h, depth_deprojection_config.cpp/h, voxelization_config.cpp/h, marching_cubes_config.cpp/h, CDO.h, cdo.cpp
- Add: compute_operation.cpp/h, depth_deprojection_op.cpp/h, voxelization_op.cpp/h, marching_cubes_op.cpp/h

### Step 7: Delete old files
- compute_pipeline.h, compute_pipeline.cpp
- depth_deprojection_config.h, depth_deprojection_config.cpp
- voxelization_config.h, voxelization_config.cpp
- marching_cubes_config.h, marching_cubes_config.cpp
- CDO.h, cdo.cpp

## What stays the same
- All GLSL shaders (untouched)
- ring_buffer.h (used internally by operations)
- VoxelVolume (external resource, referenced by operations)
- GpuMesh (external resource, referenced by MarchingCubesOp)
- ArDepthImage (external resource, provides depth buffer)
- All barriers produce identical GPU behavior
- Lazy initialization pattern preserved (Build() called when dimensions known)

## Ring buffer advancement
Each operation advances its own internal ring buffers in RecordCommands(). The frame index is passed in and used to select the correct descriptor set (indexed, not ring-advanced). Output buffer ring advancement happens inside the operation.
