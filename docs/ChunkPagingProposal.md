# Chunk Paging & GPU Slab Allocator — Design Proposal

## Problem

At 1cm voxel resolution, scanning beyond a small object (e.g. a full room)
causes OOM crashes. Two independent memory pools are unbounded:

1. **CPU (OpenChisel)**: Every chunk's full TSDF grid stays in RAM.
   At 16^3 voxels x 8 bytes = 32 KB per chunk, 10K chunks = 320 MB.
2. **GPU (consolidated mesh)**: `ConsolidateChunkMeshes()` copies ALL chunk
   meshes into a single buffer every mesh frame. 10K chunks x ~1200 avg
   verts x 32 bytes = ~384 MB, plus indices.

Both grow without bound as the user scans.

Additionally, `ConsolidateChunkMeshes()` is a performance bottleneck: it
re-walks all chunk meshes (~350+) every mesh frame, producing 150-250ms
stalls even when only 10-30 chunks actually changed.

## Solution: Three-Tier Paging

Inspired by how virtual memory works with hot/warm/cold pages:

```
┌─────────────────────────────────────────────────────┐
│  Tier 1 — GPU (hot)                                 │
│  Fixed-size slab buffer, only VISIBLE chunks.       │
│  Budget: ~512 slots x 160 KB = ~80 MB               │
│  Eviction: frustum culling each mesh frame.         │
├─────────────────────────────────────────────────────┤
│  Tier 2 — CPU/RAM (warm)                            │
│  OpenChisel's chunk map, nearby chunks.             │
│  Budget: MAX_CHUNKS (e.g. 2000) = ~64 MB            │
│  Eviction: distance-based, serialized to disk.      │
├─────────────────────────────────────────────────────┤
│  Tier 3 — Disk (cold)                               │
│  Serialized TSDF chunks in app-internal storage.    │
│  Budget: unlimited (phone storage).                 │
│  Reload: on demand when camera approaches.          │
└─────────────────────────────────────────────────────┘
```

Each tier has a hard budget. When a tier is full, the most distant entries
are demoted to the next tier down. When the camera moves back toward a
cold region, chunks are promoted upward. Data is never destroyed — only
paged out.

## Layer 1: GPU Slab Allocator + Frustum Culling

This is the most impactful change and should be implemented first. It fixes
the GPU OOM and eliminates the ConsolidateChunkMeshes bottleneck.

### Slab allocator

A fixed-size GPU buffer divided into N equally-sized slots:

```
GPU Buffer (fixed, allocated once):
┌────────┬────────┬────────┬────────┬─────┬──────────┐
│ Slot 0 │ Slot 1 │ Slot 2 │ Slot 3 │ ... │ Slot N-1 │
│ ChunkA │  FREE  │ ChunkC │ ChunkD │     │   FREE   │
└────────┴────────┴────────┴────────┴─────┴──────────┘
```

**Slot sizing**: Each 16^3 chunk can produce at most ~4096 vertices and
~8192 indices via marching cubes (surface crosses at most ~half the
voxels). Per slot:
- Vertex data: 4096 verts x 32 bytes (pos + normal + uv) = 128 KB
- Index data: 8192 indices x 4 bytes = 32 KB
- **Total per slot: ~160 KB**

**Slot count**: 512 slots = ~80 MB total. This is the hard GPU memory cap.
On devices with less RAM, reduce to 256 slots (~40 MB).

**Data structures** (CPU side):

```cpp
struct SlabAllocator {
    static constexpr int MAX_SLOTS = 512;
    static constexpr int MAX_VERTS_PER_SLOT = 4096;
    static constexpr int MAX_INDICES_PER_SLOT = 8192;

    // slot_id -> chunk currently occupying it (or empty)
    std::array<std::optional<chisel::ChunkID>, MAX_SLOTS> slotToChunk;

    // chunk -> slot_id (reverse lookup for O(1) updates)
    std::unordered_map<chisel::ChunkID, int, chisel::ChunkHasher> chunkToSlot;

    // Free slot stack for O(1) alloc/free
    std::vector<int> freeSlots;  // initially [0, 1, 2, ..., N-1]

    int Allocate(const chisel::ChunkID& id);  // pop from freeSlots
    void Free(int slotIndex);                  // push to freeSlots
    int Find(const chisel::ChunkID& id);       // lookup in chunkToSlot
};
```

### Frustum culling

Each mesh frame, before uploading:

1. Extract the camera's view-projection matrix.
2. For each chunk in OpenChisel's map, test its AABB against the frustum.
   - AABB: `origin` to `origin + chunkSize * resolution` (16cm^3 at 1cm).
   - Use a 6-plane frustum test (cheap, ~20 multiplies per chunk).
3. Classify chunks into three sets:
   - **Still visible**: already in a slot, still in frustum → no action
     (unless mesh changed → re-upload that slot)
   - **Newly visible**: in frustum but not in a slot → allocate slot, upload
   - **No longer visible**: in a slot but not in frustum → free slot

```
Each mesh frame:
  visibleSet = frustumCull(allChunks, camera)
  dirtySet   = openChisel.meshesToUpdate  (chunks whose TSDF changed)

  for chunk in (slotsInUse - visibleSet):   // left the frustum
      slab.Free(chunk)

  for chunk in (visibleSet - slotsInUse):   // entered the frustum
      if slab.hasFreeSlots():
          slot = slab.Allocate(chunk)
          uploadMeshToSlot(slot, chunk.mesh)
      else:
          skip (or evict farthest visible chunk)

  for chunk in (dirtySet ∩ slotsInUse):     // mesh changed, still visible
      uploadMeshToSlot(slab.Find(chunk), chunk.mesh)
```

### Draw call

Instead of one big `vkCmdDrawIndexed` over the full consolidated buffer,
draw each occupied slot individually — or use indirect drawing:

```cpp
// Option A: one draw per slot (simple)
for (int i = 0; i < MAX_SLOTS; i++) {
    if (slotToChunk[i].has_value()) {
        vkCmdDrawIndexed(cmd, slotIndexCount[i], 1,
                         i * MAX_INDICES_PER_SLOT,
                         i * MAX_VERTS_PER_SLOT, 0);
    }
}

// Option B: indirect draw buffer (one vkCmdDrawIndexedIndirect, GPU-driven)
// Build a VkDrawIndexedIndirectCommand array on CPU, upload once.
// More efficient when slot count is high.
```

### What this replaces

`ConsolidateChunkMeshes()` is deleted entirely. No more full re-copy every
mesh frame. Uploads are per-slot (128 KB each), only for changed or newly
visible chunks. Typical frame: 5-15 slot uploads instead of 350+ chunk copies.

### Expected performance

| Metric | Current | With slab |
|--------|---------|-----------|
| GPU memory | Unbounded (~400K verts) | Fixed 80 MB cap |
| Consolidate time | 15 ms (all chunks) | <1 ms (only dirty slots) |
| Mesh frame total | 150-250 ms | ~100-150 ms (mesh extraction dominates) |
| Room-scale scan | OOM crash | Works (GPU only holds visible) |


## Layer 2: CPU Budget with Disk Paging

Fixes CPU RAM OOM. Allows scanning an entire apartment.

### Overview

When OpenChisel's chunk count exceeds `MAX_CHUNKS`, the farthest chunks
are serialized to disk before removal. When the camera moves back toward a
serialized region, chunks are loaded back.

This is already designed in detail in
`CLAUDE_CODE_READ_THIS_WHEN_YOU_DO_CHUNK_PAGING_PLS.md`. Key additions
beyond that document:

### Interaction with the GPU slab

When a chunk is paged out (CPU → disk):
1. If it has a GPU slot, free the slot first.
2. Serialize the TSDF voxels to disk.
3. Remove from OpenChisel's map.

When a chunk is paged in (disk → CPU):
1. Deserialize from disk into OpenChisel's map.
2. Mark it dirty so `UpdateMeshes()` regenerates its mesh.
3. On the next mesh frame, frustum culling will assign it a GPU slot if visible.

### Budgets

| Resource | Budget | Rationale |
|----------|--------|-----------|
| GPU slots | 512 | ~80 MB, fits in mobile GPU memory |
| CPU chunks (RAM) | 2000 | 2000 x 32 KB = 64 MB TSDF data |
| Disk chunks | Unlimited | ~32 KB each, phone has 64-256 GB |

### Eviction policy

Distance from camera, same as current `PruneDistantChunks`. But instead of
destroying the chunk, serialize it first. Use hysteresis to prevent
thrashing:
- **Page-out radius**: chunks farther than `R_out` meters from camera
- **Page-in radius**: chunks closer than `R_in` meters, where `R_in < R_out`
- The gap `R_out - R_in` prevents load-evict-load-evict cycles.

### Serialization format

Binary, one file per chunk. See `CLAUDE_CODE_READ_THIS_WHEN_YOU_DO_CHUNK_PAGING_PLS.md`
for the exact format. 48-byte header + 32 KB voxel payload per chunk.

### I/O threading

All disk I/O happens on the existing worker thread (never on the render
thread). Paging in is rate-limited to avoid stalling integration:
- Max 5 chunks loaded per frame
- Loaded chunks are marked dirty and meshed on the next mesh pass


## Layer 3: Background Prefetch (Polish)

Optional optimization to hide page-in latency.

### Camera velocity prediction

Track camera position over the last N frames. Extrapolate the movement
direction. Pre-load chunks along the predicted path before they enter the
frustum.

### Prefetch queue

A low-priority background queue that loads chunks from disk ahead of time.
If the camera changes direction, the queue is flushed and repopulated.

This layer is nice-to-have. Layers 1+2 solve the hard problems.


## Implementation Order

| Step | What | Effort | Impact |
|------|------|--------|--------|
| 1 | GPU slab allocator + frustum culling | Medium | Fixes GPU OOM, kills consolidation stutter |
| 2 | Replace ConsolidateChunkMeshes with slab uploads | Medium | Completes Layer 1 |
| 3 | ChunkSerializer (save/load) | Small | Prerequisite for Layer 2 |
| 4 | PruneDistantChunks → page to disk | Small | Fixes CPU RAM OOM |
| 5 | Page-in on camera return | Medium | Room-scale scanning works |
| 6 | Prefetch (Layer 3) | Small | Polish, no new capability |

Steps 1-2 should be done together as they replace the same code path.
Steps 3-5 build on the serialization format already designed.


## MutableMesh Impact

The current `MutableMesh` class accepts a single vertex+index array and
uploads it as one buffer. With the slab approach, we need either:

- **Option A**: Replace MutableMesh with a single large pre-allocated buffer
  and use `vkCmdCopyBuffer` to update individual slot regions. This is the
  cleanest approach.
- **Option B**: Keep MutableMesh but give it a fixed-size backing buffer and
  add a `UpdateRegion(offset, size, data)` method for partial uploads.

Option A is recommended — it's simpler and avoids ring-buffer complexity
for per-slot updates (the slab itself handles versioning).


## Open Questions

1. **Slot size tuning**: 4096 verts max per slot is conservative. Profiling
   real scans will show the actual per-chunk max. If it's consistently under
   2048, halving the slot size doubles capacity.

2. **Indirect draw vs per-slot draw**: Indirect drawing is cleaner but
   requires maintaining a GPU-side draw command buffer. For 512 slots,
   per-slot draw calls are fine. Revisit if slot count grows.

3. **Chunk mesh caching**: When a chunk leaves the frustum and later
   re-enters, should we cache its mesh on CPU to avoid re-running marching
   cubes? OpenChisel already stores per-chunk meshes in `GetAllMeshes()`,
   so this may already work — needs verification.
