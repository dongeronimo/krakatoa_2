# Chunk Paging: Persist and Reload TSDF Chunks

## Problem

We prune distant chunks to stay under `MAX_CHUNKS` (2000), but this throws away
already-scanned world data. If the user walks back to a previously scanned area,
the reconstruction is gone.

## Goal

Turn pruning into paging: evicted chunks go to disk, and get reloaded when the
camera returns nearby.

## Open Chisel Internals Relevant to This

- Chunks are stored in `ChunkManager` as `std::unordered_map<ChunkID, ChunkPtr, ChunkHasher>`
- `ChunkID` is `Eigen::Vector3i` (3 ints: grid coordinates)
- Each chunk contains:
  - `ChunkID ID` (Eigen::Vector3i)
  - `Eigen::Vector3i numVoxels` (e.g. 16,16,16)
  - `float voxelResolutionMeters`
  - `Vec3 origin` (3 floats, world-space position)
  - `std::vector<DistVoxel> voxels` (the TSDF data)
  - `std::vector<ColorVoxel> colors` (unused by us currently)
- `DistVoxel` is POD: just `float sdf` + `float weight` (8 bytes)
- For 16^3 chunks: 4096 voxels x 8 bytes = **32 KB per chunk**
- Access: `chisel_->GetMutableChunkManager().GetMutableChunks()` returns the map
- Remove: `chunkManager.RemoveChunk(ChunkID)` erases chunk + its mesh
- There is **no built-in serialization** in Open Chisel

## Serialization Format (Suggested)

Binary, one file per chunk. Filename derived from ChunkID: `chunk_{x}_{y}_{z}.bin`

```
Header (fixed size):
  int32_t  chunkID[3]          // 12 bytes
  int32_t  numVoxels[3]        // 12 bytes (always 16,16,16 for us but store it)
  float    voxelResolution     //  4 bytes
  float    origin[3]           // 12 bytes
  uint32_t voxelCount          //  4 bytes (= numVoxels.x * y * z)
  uint32_t reserved            //  4 bytes (future use, e.g. color flag)
                               // --------
                               // 48 bytes total header

Payload:
  DistVoxel[voxelCount]        // voxelCount * 8 bytes (memcpy-safe, POD)
```

Total per chunk: 48 + 32768 = ~32 KB. At 10,000 stored chunks: ~320 MB on disk.

## Storage Location

Use Android app internal storage:
`context.getFilesDir() + "/tsdf_chunks/"` (passed to native via JNI at init time).

This avoids needing storage permissions and gets cleaned up on app uninstall.

## Implementation Plan

### 1. ChunkSerializer (new class)

```
class ChunkSerializer {
    ChunkSerializer(const std::string& storageDir);
    bool SaveChunk(const chisel::ChunkPtr& chunk);
    chisel::ChunkPtr LoadChunk(const chisel::ChunkID& id,
                               float voxelResolution,
                               bool useColor);
    bool HasChunk(const chisel::ChunkID& id) const;
    void DeleteAll();  // for "new scan" / reset
};
```

- `SaveChunk`: writes `chunk_{x}_{y}_{z}.bin` to `storageDir`
- `LoadChunk`: reads the file, reconstructs a `chisel::Chunk`, returns it
- `HasChunk`: checks if the file exists (use a cached `std::unordered_set<ChunkID>`)
- File I/O should use plain `fopen`/`fwrite` (no C++ streams on Android NDK for perf)

### 2. Modify PruneDistantChunks

Current flow:
```
over budget -> sort by distance -> RemoveChunk (data lost)
```

New flow:
```
over budget -> sort by distance -> SaveChunk to disk -> RemoveChunk
```

### 3. Add Chunk Reload in ProcessFrame

After integration, before `ConsolidateChunkMeshes`:

```
1. Determine which ChunkIDs are "nearby" the camera (within reload radius)
2. For each nearby ID not already in the live ChunkManager:
   a. If serializer.HasChunk(id): load it, insert into ChunkManager
   b. Mark it dirty so its mesh gets regenerated
3. Cap reload count per frame (e.g. 5 chunks/frame) to avoid stalls
```

The reload radius should be slightly smaller than the prune radius to avoid
thrashing (load-prune-load-prune cycles).

### 4. Wire Up Storage Path

In Java/Kotlin side, pass `getFilesDir()` path to native init via JNI.
Store it in `ChiselManager` and forward to `ChunkSerializer`.

### 5. Handle Reset

When `EVT_RESET_VOLUME` fires, call `serializer.DeleteAll()` in addition to
`chisel_->Reset()` so stale chunk files don't get reloaded into a new scan.

## Performance Considerations

- Disk I/O is on the worker thread (not render thread), so it won't cause jank
- Saving 10-20 chunks at ~32 KB each during a prune pass: <1 MB, fast on flash
- Loading should be capped per frame to avoid integration stalls
- Consider an LRU index file if the number of on-disk chunks gets very large
  (10K+), to avoid filesystem overhead from too many small files. Alternative:
  pack chunks into a single file with an index table at the end.

## Mesh Regeneration After Reload

Reloaded chunks won't have meshes. After inserting them back into the
ChunkManager, mark them via `chisel_->GetMutableChunkManager()` so
`UpdateMeshes()` picks them up. Open Chisel tracks "dirty" chunks internally
during integration, but manually inserted chunks may need to be added to the
meshes-to-update set. Check `ChunkManager::GetChunksMutable()` and the dirty
flag on `Chunk` for how to trigger re-meshing.
