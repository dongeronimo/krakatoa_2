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
                                   int threadCount) {
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
        auto truncator = std::make_shared<chisel::ConstantTruncator>(truncationDist);
        auto weighter = std::make_shared<chisel::ConstantWeighter>(1.0f);

        const chisel::Vec3List& centroids = chisel_->GetChunkManager().GetCentroids();
        LOGI("ChiselManager: centroid count = %zu (expected %d)",
             centroids.size(), chunkSizeVoxels * chunkSizeVoxels * chunkSizeVoxels);
        integrator_ = chisel::ProjectionIntegrator(truncator, weighter,
                                                    carvingDist, enableCarving,
                                                    centroids);

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
                LOGI("ChiselManager: volume reset");
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
        // Clamp to [nearPlane, farPlane] — values outside this range are invalid
        // and would cause IntegrateDepthScan to create a huge frustum, leading to OOM.
        constexpr float kNearPlane = 0.1f;
        constexpr float kFarPlane = 3.5f;
        auto depthImage = std::make_shared<chisel::DepthImage<float>>(input.width, input.height);
        float* depthPtr = depthImage->GetMutableData();
        for (int i = 0; i < input.width * input.height; i++) {
            uint16_t raw = input.depthData[i];
            float d = (raw == 0) ? 0.0f : static_cast<float>(raw) / 1000.0f;
            depthPtr[i] = (d >= kNearPlane && d <= kFarPlane) ? d : 0.0f;
        }

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
        camera.SetFarPlane(kFarPlane);

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

        // Prune distant chunks if we're over budget
        Eigen::Vector3f camPos = cameraPose.translation();
        PruneDistantChunks(camPos);

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
            ConsolidateChunkMeshes(meshOut);
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
            LOGI("[TSDF] frame %d MESH — %zu chunks (%zu→%zu), %u verts, %u tris | integrate=%ldms mesh=%ldms consolidate=%ldms | cam=(%.2f,%.2f,%.2f)",
                 frameCount, numChunks, chunksBefore, chunksAfter, numVerts, numTris,
                 integrateMs, meshMs, consolidateMs,
                 cp.x(), cp.y(), cp.z());
        } else {
            // Log every integration so we can see if the worker is alive
            size_t numChunks = chisel_->GetChunkManager().GetChunks().size();
            Eigen::Vector3f cp = cameraPose.translation();
            LOGI("[TSDF] frame %d (%d/%d) — %zu chunks (%zu→%zu) | integrate=%ldms | cam=(%.2f,%.2f,%.2f)",
                 frameCount, integrationsSinceMesh_, MESH_EVERY_N_INTEGRATIONS,
                 numChunks, chunksBefore, chunksAfter,
                 integrateMs,
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

        // Remove farthest chunks until we're back at MAX_CHUNKS
        size_t toRemove = numChunks - MAX_CHUNKS;
        size_t removed = 0;
        for (size_t i = 0; i < toRemove && i < chunkDists.size(); i++) {
            chunkMgr.RemoveChunk(chunkDists[i].id);
            removed++;
        }

        if (removed > 0) {
            LOGI("ChiselManager: pruned %zu distant chunks (%zu → %zu)",
                 removed, numChunks, numChunks - removed);
        }
    }

    void ChiselManager::ConsolidateChunkMeshes(MeshOutput& out) {
        out.vertices.clear();
        out.indices.clear();

        const auto& allMeshes = chisel_->GetChunkManager().GetAllMeshes();
        // Pre-estimate capacity
        size_t totalVerts = 0, totalIndices = 0;
        for (const auto& pair : allMeshes) {
            if (pair.second && pair.second->HasVertices()) {
                totalVerts += pair.second->vertices.size();
                totalIndices += pair.second->indices.size();
            }
        }
        out.vertices.reserve(totalVerts * 8);  // 8 floats per vertex
        out.indices.reserve(totalIndices);

        uint32_t vertexOffset = 0;
        for (const auto& pair : allMeshes) {
            const auto& mesh = pair.second;
            if (!mesh || !mesh->HasVertices()) continue;

            bool hasNormals = mesh->HasNormals();
            size_t numVerts = mesh->vertices.size();

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
