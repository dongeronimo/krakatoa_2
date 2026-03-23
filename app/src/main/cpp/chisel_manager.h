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
        void Initialize(float voxelResolution = 0.01f,
                        float truncationDist = 0.04f,
                        float carvingDist = 0.04f,
                        bool enableCarving = true,
                        int chunkSizeVoxels = 16,
                        int threadCount = 0);

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

        static constexpr size_t MAX_CHUNKS = 2000;
        float truncation_ = 0.04f;  // stored from Initialize()

        /// How many TSDF integrations to run before extracting a new mesh.
        /// Lower = more responsive visuals but slower integration throughput.
        /// Higher = faster convergence & carving but choppier mesh updates.
        static constexpr int MESH_EVERY_N_INTEGRATIONS = 5;

        int integrationsSinceMesh_ = 0;

        void WorkerLoop();
        void ProcessFrame(const FrameInput& input);
        void PruneDistantChunks(const Eigen::Vector3f& cameraPos);
        void ConsolidateChunkMeshes(MeshOutput& out);
    };

} // namespace reconstruction
