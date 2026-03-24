#include "chunk_serializer.h"
#include "android_log.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

namespace reconstruction {

    ChunkSerializer::ChunkSerializer(const std::string& storageDir)
        : storageDir_(storageDir) {
        // Ensure directory exists
        mkdir(storageDir_.c_str(), 0755);

        // Scan existing files to populate the in-memory index
        DIR* dir = opendir(storageDir_.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                // Parse chunk_{x}_{y}_{z}.bin
                int x, y, z;
                if (sscanf(entry->d_name, "chunk_%d_%d_%d.bin", &x, &y, &z) == 3) {
                    diskIndex_.insert(chisel::ChunkID(x, y, z));
                }
            }
            closedir(dir);
            if (!diskIndex_.empty()) {
                LOGI("ChunkSerializer: found %zu existing chunk files in %s",
                     diskIndex_.size(), storageDir_.c_str());
            }
        }
    }

    std::string ChunkSerializer::ChunkFilePath(const chisel::ChunkID& id) const {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s/chunk_%d_%d_%d.bin",
                 storageDir_.c_str(), id.x(), id.y(), id.z());
        return std::string(buf);
    }

    bool ChunkSerializer::SaveChunk(const chisel::ChunkPtr& chunk) {
        if (!chunk || !chunk->HasVoxels()) return false;

        const std::string path = ChunkFilePath(chunk->GetID());
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) {
            LOGI("ChunkSerializer: failed to open %s for writing", path.c_str());
            return false;
        }

        // Header
        const chisel::ChunkID& id = chunk->GetID();
        int32_t chunkID[3] = { id.x(), id.y(), id.z() };
        const Eigen::Vector3i& nv = chunk->GetNumVoxels();
        int32_t numVoxels[3] = { nv.x(), nv.y(), nv.z() };
        float voxelRes = chunk->GetVoxelResolutionMeters();
        // Origin is computed from ID in OpenChisel, but store it for validation
        float origin[3] = {
            static_cast<float>(nv.x() * id.x()) * voxelRes,
            static_cast<float>(nv.y() * id.y()) * voxelRes,
            static_cast<float>(nv.z() * id.z()) * voxelRes
        };
        uint32_t voxelCount = static_cast<uint32_t>(chunk->GetTotalNumVoxels());
        uint32_t reserved = 0;

        fwrite(chunkID, sizeof(int32_t), 3, f);
        fwrite(numVoxels, sizeof(int32_t), 3, f);
        fwrite(&voxelRes, sizeof(float), 1, f);
        fwrite(origin, sizeof(float), 3, f);
        fwrite(&voxelCount, sizeof(uint32_t), 1, f);
        fwrite(&reserved, sizeof(uint32_t), 1, f);

        // Payload: sdf/weight pairs
        const auto& voxels = chunk->GetVoxels();
        for (uint32_t i = 0; i < voxelCount; i++) {
            float sdf = voxels[i].GetSDF();
            float weight = voxels[i].GetWeight();
            fwrite(&sdf, sizeof(float), 1, f);
            fwrite(&weight, sizeof(float), 1, f);
        }

        fclose(f);
        diskIndex_.insert(chunk->GetID());
        return true;
    }

    chisel::ChunkPtr ChunkSerializer::LoadChunk(const chisel::ChunkID& id,
                                                  float voxelResolution,
                                                  const Eigen::Vector3i& numVoxels,
                                                  bool useColor) {
        const std::string path = ChunkFilePath(id);
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return nullptr;

        // Read and validate header
        int32_t fileChunkID[3];
        int32_t fileNumVoxels[3];
        float fileVoxelRes;
        float fileOrigin[3];
        uint32_t fileVoxelCount;
        uint32_t fileReserved;

        size_t r = 0;
        r += fread(fileChunkID, sizeof(int32_t), 3, f);
        r += fread(fileNumVoxels, sizeof(int32_t), 3, f);
        r += fread(&fileVoxelRes, sizeof(float), 1, f);
        r += fread(fileOrigin, sizeof(float), 3, f);
        r += fread(&fileVoxelCount, sizeof(uint32_t), 1, f);
        r += fread(&fileReserved, sizeof(uint32_t), 1, f);

        if (r != 12) { // 3+3+1+3+1+1 = 12 reads
            LOGI("ChunkSerializer: corrupt header in %s", path.c_str());
            fclose(f);
            return nullptr;
        }

        // Validate chunk ID matches
        if (fileChunkID[0] != id.x() || fileChunkID[1] != id.y() || fileChunkID[2] != id.z()) {
            LOGI("ChunkSerializer: ID mismatch in %s", path.c_str());
            fclose(f);
            return nullptr;
        }

        // Create the chunk (constructor allocates voxels and computes origin)
        auto chunk = std::make_shared<chisel::Chunk>(id, numVoxels, voxelResolution, useColor);

        uint32_t expectedCount = static_cast<uint32_t>(chunk->GetTotalNumVoxels());
        if (fileVoxelCount != expectedCount) {
            LOGI("ChunkSerializer: voxel count mismatch in %s (file=%u, expected=%u)",
                 path.c_str(), fileVoxelCount, expectedCount);
            fclose(f);
            return nullptr;
        }

        // Read voxel data
        for (uint32_t i = 0; i < fileVoxelCount; i++) {
            float sdf, weight;
            if (fread(&sdf, sizeof(float), 1, f) != 1 ||
                fread(&weight, sizeof(float), 1, f) != 1) {
                LOGI("ChunkSerializer: truncated voxel data in %s at voxel %u", path.c_str(), i);
                fclose(f);
                return nullptr;
            }
            chunk->GetDistVoxelMutable(static_cast<chisel::VoxelID>(i)).SetSDF(sdf);
            chunk->GetDistVoxelMutable(static_cast<chisel::VoxelID>(i)).SetWeight(weight);
        }

        fclose(f);
        return chunk;
    }

    bool ChunkSerializer::HasChunk(const chisel::ChunkID& id) const {
        return diskIndex_.count(id) > 0;
    }

    void ChunkSerializer::DeleteAll() {
        DIR* dir = opendir(storageDir_.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                // Only delete chunk files
                int x, y, z;
                if (sscanf(entry->d_name, "chunk_%d_%d_%d.bin", &x, &y, &z) == 3) {
                    std::string path = storageDir_ + "/" + entry->d_name;
                    unlink(path.c_str());
                }
            }
            closedir(dir);
        }
        size_t count = diskIndex_.size();
        diskIndex_.clear();
        if (count > 0) {
            LOGI("ChunkSerializer: deleted %zu chunk files", count);
        }
    }

} // namespace reconstruction
