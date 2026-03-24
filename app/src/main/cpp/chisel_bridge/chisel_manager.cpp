#include "chisel_manager.h"
#include "android_log.h"
#include <algorithm>
#include <chrono>
#include <Eigen/Dense>

namespace reconstruction {

    ChiselManager::ChiselManager() = default;

    ChiselManager::~ChiselManager() {
        if (running_) {
            running_ = false;
            cv_.notify_one();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void ChiselManager::Initialize(float voxelResolution,
                                   float truncationDist,
                                   float carvingDist,
                                   bool enableCarving,
                                   int chunkSizeVoxels,
                                   int threadCount,
                                   const std::string& storagePath) {
        if (initialized_) return;

        // Auto-detect thread count: use half the available cores
        if (threadCount <= 0) {
            int hw = static_cast<int>(std::thread::hardware_concurrency());
            numThreads_ = std::max(1, hw / 2);
        } else {
            numThreads_ = threadCount;
        }
        LOGI("ChiselManager: using %d worker threads (hardware_concurrency=%u)",
             numThreads_, std::thread::hardware_concurrency());

        // Create the chisel volume
        Eigen::Vector3i chunkSize(chunkSizeVoxels, chunkSizeVoxels, chunkSizeVoxels);
        chisel_ = std::make_shared<chisel::Chisel>(chunkSize, voxelResolution, false);

        // Set up the projection integrator with pre-computed voxel centroids.
        // The centroids are the 3D positions of each voxel center within a canonical chunk.
        // Without these, the integrator's inner loop is empty and no voxels get updated.
        truncation_ = truncationDist;
        voxelResolution_ = voxelResolution;
        chunkSizeVoxels_ = chunkSizeVoxels;
        auto truncator = std::make_shared<chisel::ConstantTruncator>(truncationDist);
        auto weighter = std::make_shared<chisel::ConstantWeighter>(1.0f);

        const chisel::Vec3List& centroids = chisel_->GetChunkManager().GetCentroids();
        LOGI("ChiselManager: centroid count = %zu (expected %d)",
             centroids.size(), chunkSizeVoxels * chunkSizeVoxels * chunkSizeVoxels);
        integrator_ = chisel::ProjectionIntegrator(truncator, weighter,
                                                    carvingDist, enableCarving,
                                                    centroids);

        // Initialize disk paging if storage path is provided
        if (!storagePath.empty()) {
            serializer_ = std::make_unique<ChunkSerializer>(storagePath);
            LOGI("ChiselManager: disk paging enabled at %s (%zu existing chunks)",
                 storagePath.c_str(), serializer_->DiskChunkCount());
        } else {
            LOGI("ChiselManager: disk paging disabled (no storage path)");
        }

        // Start worker thread
        running_ = true;
        worker_ = std::thread(&ChiselManager::WorkerLoop, this);

        initialized_ = true;
        LOGI("ChiselManager: initialized (voxelRes=%.3f, truncDist=%.3f, chunkSize=%d, carving=%s)",
             voxelResolution, truncationDist, chunkSizeVoxels,
             enableCarving ? "on" : "off");
    }

    void ChiselManager::IntegrateFrame(const FrameInput& input) {
        // Store input in the latest-wins slot
        {
            std::lock_guard<std::mutex> lock(inputMutex_);
            latestInput_ = input;
            hasNewInput_ = true;
        }
        // Post a zero-payload notification event and wake the worker
        toWorker_.Post(EVT_INTEGRATE_FRAME);
        cv_.notify_one();
    }

    bool ChiselManager::PollMesh(MeshOutput& out) {
        bool gotMesh = false;
        toRender_.Drain([&](const threading::Event& e) {
            if (e.id == EVT_MESH_READY && !e.payload.empty()) {
                // Deserialize: vertexCount (uint32), indexCount (uint32),
                //              vertex data (floats), index data (uint32s)
                const uint8_t* ptr = e.payload.data();
                uint32_t vertexFloats = *reinterpret_cast<const uint32_t*>(ptr);
                ptr += sizeof(uint32_t);
                uint32_t indexCount = *reinterpret_cast<const uint32_t*>(ptr);
                ptr += sizeof(uint32_t);

                out.vertices.resize(vertexFloats);
                memcpy(out.vertices.data(), ptr, vertexFloats * sizeof(float));
                ptr += vertexFloats * sizeof(float);

                out.indices.resize(indexCount);
                memcpy(out.indices.data(), ptr, indexCount * sizeof(uint32_t));

                gotMesh = true;
            }
        });
        return gotMesh;
    }

    void ChiselManager::WorkerLoop() {
        LOGI("ChiselManager: worker thread started");
        while (running_) {
            // Wait for a signal from the render thread
            {
                std::unique_lock<std::mutex> lock(cvMutex_);
                cv_.wait(lock, [this]() {
                    return !running_ || !toWorker_.Empty();
                });
            }
            if (!running_) break;

            // Drain all pending events — we only care about the latest frame
            bool shouldReset = false;
            toWorker_.Drain([&](const threading::Event& e) {
                if (e.id == EVT_RESET_VOLUME) shouldReset = true;
            });

            if (shouldReset && chisel_) {
                chisel_->Reset();
                integrationsSinceMesh_ = 0;
                // Clear all serialized chunks on reset
                if (serializer_) {
                    serializer_->DeleteAll();
                }
                LOGI("ChiselManager: volume reset (disk chunks cleared)");
            }

            // Grab the latest input and process it
            FrameInput input;
            {
                std::lock_guard<std::mutex> lock(inputMutex_);
                if (!hasNewInput_) continue;
                input = std::move(latestInput_);
                hasNewInput_ = false;
            }
            ProcessFrame(input);
        }
        LOGI("ChiselManager: worker thread stopped");
    }

    void ChiselManager::ProcessFrame(const FrameInput& input) {
        if (input.depthData.empty() || input.width <= 0 || input.height <= 0) {
            LOGI("[TSDF] ProcessFrame: skipping (empty=%d, w=%d, h=%d)",
                 (int)input.depthData.empty(), input.width, input.height);
            return;
        }

        // Build the depth image (convert uint16 mm → float meters)
        // kFarPlane caps the maximum integration depth. OpenChisel internally
        // builds its frustum from the actual min/max depth in the image (via
        // GetStats), so this cap directly controls frustum size.
        // At 1cm voxels, 1.5m gives ~600 frustum chunks vs ~2000 at 3.5m.
        constexpr float kNearPlane = 0.1f;
        constexpr float kFarPlane = 1.5f;
        auto depthImage = std::make_shared<chisel::DepthImage<float>>(input.width, input.height);
        float* depthPtr = depthImage->GetMutableData();
        float maxObservedDepth = 0.0f;
        for (int i = 0; i < input.width * input.height; i++) {
            uint16_t raw = input.depthData[i];
            float d = (raw == 0) ? 0.0f : static_cast<float>(raw) / 1000.0f;
            if (d >= kNearPlane && d <= kFarPlane) {
                depthPtr[i] = d;
                if (d > maxObservedDepth) maxObservedDepth = d;
            } else {
                depthPtr[i] = 0.0f;
            }
        }

        // If no valid depth pixels in this frame, skip integration entirely
        if (maxObservedDepth <= 0.0f) return;

        float effectiveFar = maxObservedDepth;

        // Set up camera intrinsics
        chisel::PinholeCamera camera;
        chisel::Intrinsics intrinsics;
        intrinsics.SetFx(input.fx);
        intrinsics.SetFy(input.fy);
        intrinsics.SetCx(input.cx);
        intrinsics.SetCy(input.cy);
        camera.SetIntrinsics(intrinsics);
        camera.SetWidth(input.width);
        camera.SetHeight(input.height);
        camera.SetNearPlane(kNearPlane);
        camera.SetFarPlane(effectiveFar);

        // Convert view matrix (world→camera, column-major) to camera pose (camera→world).
        // ARCore uses OpenGL convention (forward = -Z, up = +Y).
        // Open Chisel uses CV convention  (forward = +Z, up = -Y).
        // After inverting the view matrix to get camera→world, we right-multiply
        // by a Y/Z flip so that Open Chisel's unprojected points (z = +depth)
        // end up in front of the camera rather than behind it.
        Eigen::Map<const Eigen::Matrix4f> viewMat(input.viewMatrix.data());
        Eigen::Matrix4f glToCv = Eigen::Matrix4f::Identity();
        glToCv(1, 1) = -1.0f;  // Y: up → down
        glToCv(2, 2) = -1.0f;  // Z: back → forward
        Eigen::Affine3f cameraPose;
        cameraPose.matrix() = viewMat.inverse() * glToCv;

        size_t chunksBefore = chisel_->GetChunkManager().GetChunks().size();

        // ── TSDF integration (runs every frame) ─────────────────────────
        auto t0 = std::chrono::steady_clock::now();

        chisel_->IntegrateDepthScan<float>(integrator_,
                                           depthImage,
                                           cameraPose,
                                           camera);

        auto t1 = std::chrono::steady_clock::now();

        size_t chunksAfter = chisel_->GetChunkManager().GetChunks().size();

        // Prune distant chunks if we're over budget (serializes to disk first)
        Eigen::Vector3f camPos = cameraPose.translation();
        PruneDistantChunks(camPos);

        // Reload nearby chunks from disk (if paging is enabled)
        ReloadNearbyChunks(camPos);

        integrationsSinceMesh_++;

        static int frameCount = 0;
        frameCount++;

        long integrateMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        // ── Mesh extraction (expensive — only every N integrations) ─────
        // Marching cubes + consolidation is the bottleneck. By batching N
        // integrations before meshing, the worker processes frames faster
        // and carving gets more iterations to actually clear geometry.
        if (integrationsSinceMesh_ >= MESH_EVERY_N_INTEGRATIONS) {
            integrationsSinceMesh_ = 0;

            auto t2 = std::chrono::steady_clock::now();
            chisel_->UpdateMeshes();
            auto t3 = std::chrono::steady_clock::now();

            MeshOutput meshOut;
            ConsolidateChunkMeshes(meshOut, camPos);
            auto t4 = std::chrono::steady_clock::now();

            long meshMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
            long consolidateMs = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();

            if (!meshOut.indices.empty()) {
                // Serialize: [vertexFloats(u32), indexCount(u32), vertex data, index data]
                uint32_t vertexFloats = static_cast<uint32_t>(meshOut.vertices.size());
                uint32_t indexCount = static_cast<uint32_t>(meshOut.indices.size());
                size_t totalSize = 2 * sizeof(uint32_t)
                                 + vertexFloats * sizeof(float)
                                 + indexCount * sizeof(uint32_t);

                std::vector<uint8_t> payload(totalSize);
                uint8_t* ptr = payload.data();
                memcpy(ptr, &vertexFloats, sizeof(uint32_t)); ptr += sizeof(uint32_t);
                memcpy(ptr, &indexCount, sizeof(uint32_t)); ptr += sizeof(uint32_t);
                memcpy(ptr, meshOut.vertices.data(), vertexFloats * sizeof(float)); ptr += vertexFloats * sizeof(float);
                memcpy(ptr, meshOut.indices.data(), indexCount * sizeof(uint32_t));

                toRender_.Post(EVT_MESH_READY, payload.data(), payload.size());
            }

            // Always log mesh extraction — this is the critical event
            size_t numChunks = chisel_->GetChunkManager().GetChunks().size();
            uint32_t numVerts = static_cast<uint32_t>(meshOut.vertices.size() / 8);
            uint32_t numTris = static_cast<uint32_t>(meshOut.indices.size() / 3);
            Eigen::Vector3f cp = cameraPose.translation();
            size_t diskChunks = serializer_ ? serializer_->DiskChunkCount() : 0;
            LOGI("[TSDF] frame %d MESH — %zu chunks (ram) + %zu (disk), %u verts, %u tris | integrate=%ldms mesh=%ldms consolidate=%ldms | far=%.2f | cam=(%.2f,%.2f,%.2f)",
                 frameCount, numChunks, diskChunks, numVerts, numTris,
                 integrateMs, meshMs, consolidateMs,
                 effectiveFar,
                 cp.x(), cp.y(), cp.z());
        } else {
            // Log every integration so we can see if the worker is alive
            size_t numChunks = chisel_->GetChunkManager().GetChunks().size();
            Eigen::Vector3f cp = cameraPose.translation();
            size_t diskChunks = serializer_ ? serializer_->DiskChunkCount() : 0;
            LOGI("[TSDF] frame %d (%d/%d) — %zu chunks (ram) + %zu (disk) | integrate=%ldms | far=%.2f | cam=(%.2f,%.2f,%.2f)",
                 frameCount, integrationsSinceMesh_, MESH_EVERY_N_INTEGRATIONS,
                 numChunks, diskChunks,
                 integrateMs,
                 effectiveFar,
                 cp.x(), cp.y(), cp.z());
        }
    }

    void ChiselManager::PruneDistantChunks(const Eigen::Vector3f& cameraPos) {
        auto& chunkMgr = chisel_->GetMutableChunkManager();
        const auto& chunks = chunkMgr.GetChunks();
        size_t numChunks = chunks.size();

        if (numChunks <= MAX_CHUNKS) return;

        // Collect chunk IDs with their squared distances from camera
        struct ChunkDist {
            chisel::ChunkID id;
            float distSq;
        };
        std::vector<ChunkDist> chunkDists;
        chunkDists.reserve(numChunks);

        float resolution = chunkMgr.GetResolution();
        Eigen::Vector3i chunkSize = chunkMgr.GetChunkSize();
        Eigen::Vector3f halfChunk = chunkSize.cast<float>() * resolution * 0.5f;

        for (const auto& pair : chunks) {
            Eigen::Vector3f origin = pair.first.cast<float>().cwiseProduct(
                    chunkSize.cast<float>()) * resolution;
            Eigen::Vector3f center = origin + halfChunk;
            float distSq = (center - cameraPos).squaredNorm();
            chunkDists.push_back({pair.first, distSq});
        }

        // Sort by distance (farthest first)
        std::sort(chunkDists.begin(), chunkDists.end(),
                  [](const ChunkDist& a, const ChunkDist& b) { return a.distSq > b.distSq; });

        // Remove farthest chunks until we're back at MAX_CHUNKS.
        // If disk paging is enabled, serialize before removing.
        size_t toRemove = numChunks - MAX_CHUNKS;
        size_t removed = 0;
        size_t serialized = 0;
        for (size_t i = 0; i < toRemove && i < chunkDists.size(); i++) {
            const chisel::ChunkID& id = chunkDists[i].id;

            // Serialize to disk before removing (if paging is enabled)
            if (serializer_) {
                chisel::ChunkPtr chunk = chunkMgr.GetChunk(id);
                if (chunk && serializer_->SaveChunk(chunk)) {
                    serialized++;
                }
            }

            chunkMgr.RemoveChunk(id);
            removed++;
        }

        if (removed > 0) {
            LOGI("ChiselManager: pruned %zu distant chunks (%zu → %zu), %zu serialized to disk",
                 removed, numChunks, numChunks - removed, serialized);
        }
    }

    void ChiselManager::ReloadNearbyChunks(const Eigen::Vector3f& cameraPos) {
        if (!serializer_ || serializer_->DiskChunkCount() == 0) return;

        auto& chunkMgr = chisel_->GetMutableChunkManager();

        // Don't reload if we're already near the RAM budget
        if (chunkMgr.GetChunks().size() >= MAX_CHUNKS - MAX_RELOAD_PER_FRAME) return;
        float resolution = chunkMgr.GetResolution();
        Eigen::Vector3i chunkSize = chunkMgr.GetChunkSize();
        Eigen::Vector3f halfChunk = chunkSize.cast<float>() * resolution * 0.5f;
        float chunkWorldSize = chunkSize.x() * resolution;  // assumes cubic chunks

        // Determine the range of chunk grid coordinates within the reload radius
        int radiusInChunks = static_cast<int>(std::ceil(RELOAD_RADIUS / chunkWorldSize));
        chisel::ChunkID camChunk = chunkMgr.GetIDAt(cameraPos);

        int reloaded = 0;
        chisel::ChunkSet reloadedSet;

        for (int dx = -radiusInChunks; dx <= radiusInChunks && reloaded < MAX_RELOAD_PER_FRAME; dx++) {
            for (int dy = -radiusInChunks; dy <= radiusInChunks && reloaded < MAX_RELOAD_PER_FRAME; dy++) {
                for (int dz = -radiusInChunks; dz <= radiusInChunks && reloaded < MAX_RELOAD_PER_FRAME; dz++) {
                    chisel::ChunkID id(camChunk.x() + dx, camChunk.y() + dy, camChunk.z() + dz);

                    // Skip if already in RAM
                    if (chunkMgr.HasChunk(id)) continue;

                    // Skip if not on disk
                    if (!serializer_->HasChunk(id)) continue;

                    // Distance check
                    Eigen::Vector3f origin = id.cast<float>().cwiseProduct(
                            chunkSize.cast<float>()) * resolution;
                    Eigen::Vector3f center = origin + halfChunk;
                    float distSq = (center - cameraPos).squaredNorm();
                    if (distSq > RELOAD_RADIUS_SQ) continue;

                    // Load from disk
                    Eigen::Vector3i nv(chunkSizeVoxels_, chunkSizeVoxels_, chunkSizeVoxels_);
                    chisel::ChunkPtr chunk = serializer_->LoadChunk(id, voxelResolution_, nv, false);
                    if (!chunk) continue;

                    // Insert into the live chunk manager
                    chunkMgr.AddChunk(chunk);

                    // Mark for mesh regeneration (marching cubes)
                    reloadedSet[id] = true;
                    // Also mark neighbors so border geometry is seamless
                    for (int nx = -1; nx <= 1; nx++) {
                        for (int ny = -1; ny <= 1; ny++) {
                            for (int nz = -1; nz <= 1; nz++) {
                                chisel::ChunkID neighbor(id.x() + nx, id.y() + ny, id.z() + nz);
                                if (chunkMgr.HasChunk(neighbor)) {
                                    reloadedSet[neighbor] = true;
                                }
                            }
                        }
                    }

                    reloaded++;
                }
            }
        }

        if (reloaded > 0) {
            // Generate meshes for reloaded chunks (and their neighbors)
            chunkMgr.RecomputeMeshes(reloadedSet);
            LOGI("ChiselManager: reloaded %d chunks from disk, meshed %zu",
                 reloaded, reloadedSet.size());
        }
    }

    void ChiselManager::ConsolidateChunkMeshes(MeshOutput& out, const Eigen::Vector3f& cameraPos) {
        out.vertices.clear();
        out.indices.clear();

        const auto& chunkMgr = chisel_->GetChunkManager();
        const auto& allMeshes = chunkMgr.GetAllMeshes();
        float resolution = chunkMgr.GetResolution();
        Eigen::Vector3i chunkSize = chunkMgr.GetChunkSize();
        Eigen::Vector3f halfChunk = chunkSize.cast<float>() * resolution * 0.5f;

        // Collect visible chunks with distance, so we can sort nearest-first
        // and stop when the vertex budget is reached.
        struct VisibleChunk {
            chisel::ChunkID id;
            float distSq;
        };
        std::vector<VisibleChunk> visible;
        visible.reserve(allMeshes.size());

        for (const auto& pair : allMeshes) {
            if (!pair.second || !pair.second->HasVertices()) continue;

            Eigen::Vector3f origin = pair.first.cast<float>().cwiseProduct(
                    chunkSize.cast<float>()) * resolution;
            Eigen::Vector3f center = origin + halfChunk;
            float distSq = (center - cameraPos).squaredNorm();
            if (distSq > CONSOLIDATION_RADIUS_SQ) continue;

            visible.push_back({pair.first, distSq});
        }

        // Sort nearest-first so the vertex budget prioritises close geometry
        std::sort(visible.begin(), visible.end(),
                  [](const VisibleChunk& a, const VisibleChunk& b) { return a.distSq < b.distSq; });

        uint32_t vertexOffset = 0;

        for (const auto& vc : visible) {
            auto it = allMeshes.find(vc.id);
            if (it == allMeshes.end()) continue;
            const auto& mesh = it->second;
            if (!mesh || !mesh->HasVertices()) continue;

            size_t numVerts = mesh->vertices.size();

            // Stop if adding this chunk would exceed the vertex budget
            if (vertexOffset + numVerts > VERTEX_BUDGET) {
                LOGI("[TSDF] ConsolidateChunkMeshes: vertex budget reached (%u / %u), skipping %zu remaining chunks",
                     vertexOffset, VERTEX_BUDGET, visible.size() - (&vc - visible.data()));
                break;
            }

            bool hasNormals = mesh->HasNormals();

            for (size_t i = 0; i < numVerts; i++) {
                const auto& pos = mesh->vertices[i];
                out.vertices.push_back(pos.x());
                out.vertices.push_back(pos.y());
                out.vertices.push_back(pos.z());

                if (hasNormals && i < mesh->normals.size()) {
                    const auto& n = mesh->normals[i];
                    out.vertices.push_back(n.x());
                    out.vertices.push_back(n.y());
                    out.vertices.push_back(n.z());
                } else {
                    out.vertices.push_back(0.0f);
                    out.vertices.push_back(1.0f);
                    out.vertices.push_back(0.0f);
                }

                // UV = (0,0) — no texture
                out.vertices.push_back(0.0f);
                out.vertices.push_back(0.0f);
            }

            for (size_t idx : mesh->indices) {
                out.indices.push_back(vertexOffset + static_cast<uint32_t>(idx));
            }

            vertexOffset += static_cast<uint32_t>(numVerts);
        }
    }

} // namespace reconstruction
