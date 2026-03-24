#pragma once
#include <memory>
#include <vector>
#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>
#include "thread_event.h"
#include "chunk_serializer.h"
#include <open_chisel/Chisel.h>
#include <open_chisel/camera/PinholeCamera.h>
#include <open_chisel/camera/DepthImage.h>
#include <open_chisel/ProjectionIntegrator.h>
#include <open_chisel/truncation/ConstantTruncator.h>
#include <open_chisel/weighting/ConstantWeighter.h>

namespace reconstruction {

    class ChiselManager {
    public:
        // Event IDs for inter-thread communication
        enum EventIds : threading::EventId {
            // Render thread → Worker thread
            EVT_INTEGRATE_FRAME = 1,
            EVT_RESET_VOLUME    = 2,
            // Worker thread → Render thread
            EVT_MESH_READY      = 100,
        };

        // Depth frame + camera data queued for integration
        struct FrameInput {
            std::vector<uint16_t> depthData;
            int width = 0;
            int height = 0;
            float fx = 0, fy = 0, cx = 0, cy = 0;
            std::array<float, 16> viewMatrix{};  // column-major, world → camera
        };

        // Consolidated mesh output ready for GPU upload
        // Vertex layout: [pos.x, pos.y, pos.z, norm.x, norm.y, norm.z, u, v] per vertex
        struct MeshOutput {
            std::vector<float> vertices;
            std::vector<uint32_t> indices;
        };

        ChiselManager();
        ~ChiselManager();

        ChiselManager(const ChiselManager&) = delete;
        ChiselManager& operator=(const ChiselManager&) = delete;

        /// Call once when depth image dimensions are first known.
        /// threadCount=0 → auto-detect: max(1, hardware_concurrency / 2)
        /// storagePath: directory for serialized TSDF chunks (disk paging)
        void Initialize(float voxelResolution = 0.02f,
                        float truncationDist = 0.04f,
                        float carvingDist = 0.04f,
                        bool enableCarving = true,
                        int chunkSizeVoxels = 16,
                        int threadCount = 0,
                        const std::string& storagePath = "");

        bool IsInitialized() const { return initialized_; }

        /// Non-blocking: stores the frame and notifies the worker thread.
        /// If the worker is still busy, the previous queued frame is overwritten (latest-wins).
        void IntegrateFrame(const FrameInput& input);

        /// Call from render thread each frame. Returns true if a new mesh
        /// was produced since the last call; if so, writes it into |out|.
        bool PollMesh(MeshOutput& out);

    private:
        bool initialized_ = false;
        int numThreads_ = 1;

        // OpenChisel objects
        chisel::ChiselPtr chisel_;
        chisel::ProjectionIntegrator integrator_;

        // Event queues
        threading::EventQueue toWorker_;   // render → worker
        threading::EventQueue toRender_;   // worker → render

        // Latest frame input (mutex-protected slot, latest-wins)
        std::mutex inputMutex_;
        FrameInput latestInput_;
        bool hasNewInput_ = false;

        // Worker thread
        std::thread worker_;
        std::mutex cvMutex_;
        std::condition_variable cv_;
        std::atomic<bool> running_{false};

        /// Maximum number of chunks kept in RAM (CPU budget).
        static constexpr size_t MAX_CHUNKS = 8000;

        /// Maximum vertices the consolidated mesh may contain.
        /// Must be ≤ WORLD_MESH_MAX_VERTICES (the Vulkan buffer size).
        static constexpr uint32_t VERTEX_BUDGET = 1900000;

        /// Maximum distance (meters) from camera for chunks to be included in
        /// the consolidated mesh sent to the GPU. Chunks beyond this radius
        /// are still in RAM but not rendered — saves GPU bandwidth.
        static constexpr float CONSOLIDATION_RADIUS = 2.0f;
        static constexpr float CONSOLIDATION_RADIUS_SQ = CONSOLIDATION_RADIUS * CONSOLIDATION_RADIUS;

        /// Radius within which serialized (on-disk) chunks are reloaded into RAM.
        /// Must be smaller than the effective prune distance to avoid thrashing.
        static constexpr float RELOAD_RADIUS = 1.8f;
        static constexpr float RELOAD_RADIUS_SQ = RELOAD_RADIUS * RELOAD_RADIUS;

        /// Maximum number of chunks to reload from disk per integration frame.
        static constexpr int MAX_RELOAD_PER_FRAME = 5;

        float truncation_ = 0.10f;  // stored from Initialize()
        float voxelResolution_ = 0.02f;
        int chunkSizeVoxels_ = 16;

        /// How many TSDF integrations to run before extracting a new mesh.
        /// Lower = more responsive visuals but slower integration throughput.
        /// Higher = faster convergence & carving but choppier mesh updates.
        static constexpr int MESH_EVERY_N_INTEGRATIONS = 5;

        int integrationsSinceMesh_ = 0;

        // Disk paging
        std::unique_ptr<ChunkSerializer> serializer_;

        void WorkerLoop();
        void ProcessFrame(const FrameInput& input);
        void PruneDistantChunks(const Eigen::Vector3f& cameraPos);
        void ReloadNearbyChunks(const Eigen::Vector3f& cameraPos);
        void ConsolidateChunkMeshes(MeshOutput& out, const Eigen::Vector3f& cameraPos);
    };

} // namespace reconstruction
