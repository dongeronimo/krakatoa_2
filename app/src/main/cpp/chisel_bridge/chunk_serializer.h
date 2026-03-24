#pragma once
#include <string>
#include <unordered_set>
#include <open_chisel/Chunk.h>
#include <open_chisel/ChunkManager.h>

namespace reconstruction {

    /// Serializes and deserializes OpenChisel TSDF chunks to/from disk.
    ///
    /// Binary format per chunk file (chunk_{x}_{y}_{z}.bin):
    ///   Header (48 bytes):
    ///     int32_t  chunkID[3]       — 12 bytes
    ///     int32_t  numVoxels[3]     — 12 bytes
    ///     float    voxelResolution  —  4 bytes
    ///     float    origin[3]        — 12 bytes
    ///     uint32_t voxelCount       —  4 bytes
    ///     uint32_t reserved         —  4 bytes
    ///   Payload:
    ///     float[voxelCount * 2]     — (sdf, weight) pairs
    class ChunkSerializer {
    public:
        explicit ChunkSerializer(const std::string& storageDir);

        /// Save a chunk's TSDF data to disk. Returns true on success.
        bool SaveChunk(const chisel::ChunkPtr& chunk);

        /// Load a chunk from disk and reconstruct it. Returns nullptr on failure.
        chisel::ChunkPtr LoadChunk(const chisel::ChunkID& id,
                                   float voxelResolution,
                                   const Eigen::Vector3i& numVoxels,
                                   bool useColor);

        /// Check if a chunk is available on disk (uses in-memory index).
        bool HasChunk(const chisel::ChunkID& id) const;

        /// Delete all serialized chunk files and clear the index.
        void DeleteAll();

        /// Number of chunks currently stored on disk.
        size_t DiskChunkCount() const { return diskIndex_.size(); }

    private:
        std::string storageDir_;
        std::unordered_set<chisel::ChunkID, chisel::ChunkHasher> diskIndex_;

        std::string ChunkFilePath(const chisel::ChunkID& id) const;
    };

} // namespace reconstruction
