# OpenChisel Library

[OpenChisel](https://github.com/personalrobotics/OpenChisel) is an open-source C++ library for real-time 3D reconstruction using chunked, spatially-hashed TSDF volumes. Krakatoa uses it as the CPU-side reconstruction backend.

## Why OpenChisel?

Unlike a fixed-size 3D grid (which wastes memory on empty space), OpenChisel uses a **chunked architecture** where only regions near observed surfaces allocate memory. This makes it practical for mobile AR where the scene extent is unknown.

## Core Concepts

### Chunks

The volume is divided into fixed-size chunks (default: 16x16x16 voxels). Each chunk is identified by a 3D integer coordinate (`ChunkID`). Chunks are stored in a hash map, so only regions near surfaces exist in memory.

```
World space is divided into a grid of chunks:

  [Chunk(0,0,0)] [Chunk(1,0,0)] [         ] [         ]
  [Chunk(0,1,0)] [Chunk(1,1,0)] [Chunk(2,1,0)] [      ]
  [         ] [         ] [Chunk(2,2,0)] [         ]

Only chunks near surfaces are allocated (sparse).
```

### Voxels

Each voxel within a chunk stores:
- **SDF value** (float): signed distance to the nearest surface
- **Weight** (float): accumulated observation count

### Centroids

When the chunk manager is created, it pre-computes the **centroids** — the 3D world-space positions of every voxel center within a canonical chunk. These are reused for all chunks (offset by the chunk's origin). The `ProjectionIntegrator` uses these centroids to iterate over voxels efficiently.

## Key Classes

### `Chisel` (main entry point)
- Owns the `ChunkManager`
- `IntegrateDepthScan<T>()`: integrates a depth frame into the volume
- `UpdateMeshes()`: runs Marching Cubes on dirty chunks
- `GarbageCollect()`: removes empty chunks
- `Reset()`: clears the entire volume

### `ChunkManager`
- Hash map of `ChunkID → ChunkPtr`
- Manages chunk allocation, removal, and spatial queries
- `GetCentroids()`: returns pre-computed voxel center positions
- `GetAllMeshes()`: returns per-chunk extracted meshes
- `RemoveChunk()`: deletes a chunk and its mesh

### `ProjectionIntegrator`
- The core TSDF integration algorithm
- `Integrate()`: for each voxel in a chunk:
  1. Transform the voxel center to camera space (using the camera pose)
  2. Project to pixel coordinates (using `PinholeCamera::ProjectPoint`)
  3. Read the depth at that pixel
  4. Compute the signed distance (measured depth - voxel depth)
  5. If within truncation band, update the voxel's SDF and weight
- Uses a `Truncator` to define the truncation distance
- Uses a `Weighter` to define per-observation weights
- Supports **carving**: if a voxel is observed as free space (depth > voxel), its weight is reduced

### `PinholeCamera`
- Stores intrinsics (fx, fy, cx, cy) and image dimensions
- `ProjectPoint(Vec3)`: 3D → 2D projection using the pinhole model
  - Returns `(fx*x/z + cx, fy*y/z + cy, z)`
- `UnprojectPoint(Vec3)`: 2D → 3D back-projection
- Near/far plane for frustum culling

### `DepthImage<T>`
- Templated 2D image buffer (typically `float`)
- `DepthAt(row, col)`: reads depth value (note: row-major, **row first**)
- `Index(row, col) = col + row * width`
- `IsInside(row, col)`: bounds check
- `GetStats()`: computes min, max, mean (excluding zeros/NaN)

### `ConstantTruncator`
- Returns a fixed truncation distance for all voxels
- Krakatoa uses 3cm (0.03m)

### `ConstantWeighter`
- Returns a fixed weight (1.0) for all observations
- Every depth reading contributes equally

### `Mesh`
- Per-chunk triangle mesh output from Marching Cubes
- `vertices`: list of `Vec3` positions
- `normals`: list of `Vec3` per-vertex normals
- `indices`: triangle index list

## Integration Flow in Krakatoa

```
FrameInput (depth + intrinsics + view matrix)
    |
    v
ChiselManager::ProcessFrame()
    |
    ├─ Convert uint16 mm → float meters (DepthImage)
    ├─ Set up PinholeCamera with scaled intrinsics
    ├─ Convert view matrix: world→GL_camera → invert → camera→world → apply GL→CV flip
    |
    v
Chisel::IntegrateDepthScan()
    |
    ├─ Build frustum from camera + depth range
    ├─ Find intersecting chunks (spatial hash lookup)
    ├─ For each chunk: ProjectionIntegrator::Integrate()
    │   └─ For each voxel centroid:
    │       ├─ Transform to camera space
    │       ├─ Project to pixel (u,v)
    │       ├─ Read depth at (v,u) ← note row,col order
    │       ├─ Compute signed distance
    │       └─ Update voxel SDF + weight
    └─ GarbageCollect empty chunks
    |
    v
Chisel::UpdateMeshes()
    |
    └─ Marching Cubes on each dirty chunk → per-chunk Mesh
    |
    v
ChiselManager::ConsolidateChunkMeshes()
    |
    └─ Merge all chunk meshes into a single vertex/index buffer
        └─ Post to render thread via EventQueue
```

## Important Notes

### Coordinate Convention Mismatch
OpenChisel uses **computer vision (CV) convention**: +Z forward, +Y down. ARCore uses **OpenGL convention**: -Z forward, +Y up. The `ChiselManager` applies a Y/Z flip when converting the camera pose:

```cpp
glToCv(1,1) = -1.0f;  // Y: up → down
glToCv(2,2) = -1.0f;  // Z: back → forward
cameraPose = viewMat.inverse() * glToCv;
```

### DepthAt Row/Column Order
`DepthImage::DepthAt(row, col)` takes **row first**, then column. The integrator calls it as `DepthAt(cameraPos(1), cameraPos(0))` where `cameraPos(0)` is u (column) and `cameraPos(1)` is v (row).

### Sensor vs Display View Matrix
ARCore's `ArCamera_getViewMatrix()` returns a **display-oriented** view matrix (axes aligned to screen). But the depth image and `ArCamera_getImageIntrinsics()` are in **sensor coordinates** (unrotated). Krakatoa uses `ArCamera_getPose()` to get the physical sensor pose and builds the view matrix from that, ensuring it matches the depth image coordinate frame.
