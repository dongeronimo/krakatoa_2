#ifndef KRAKATOA_GPU_MESH_H
#define KRAKATOA_GPU_MESH_H
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "mesh.h"
#include <string>
#include <cstdint>

namespace graphics {
    /**
     * GPU-generated mesh for compute shader output (e.g. marching cubes).
     *
     * Unlike MutableMesh (CPU→GPU), GpuMesh lives entirely on the GPU:
     * - Vertex and index buffers are pre-allocated with a maximum capacity.
     * - An atomic counter buffer holds [vertexCount, indexCount] and is
     *   host-readable so the CPU can query counts for vkCmdDrawIndexed.
     * - The counter buffer must be reset to zero before each compute dispatch.
     *
     * Vertex format: interleaved [pos3, normal3, uv2] = 8 floats = 32 bytes,
     * matching the existing pipeline's vertex input layout.
     */
    class GpuMesh : public Mesh {
    public:
        static constexpr uint32_t FLOATS_PER_VERTEX = 8; // pos3 + normal3 + uv2
        static constexpr uint32_t BYTES_PER_VERTEX  = FLOATS_PER_VERTEX * sizeof(float);

        /**
         * @param maxVertices  Maximum number of vertices the buffer can hold
         * @param maxIndices   Maximum number of indices the buffer can hold
         */
        GpuMesh(VkDevice device,
                VmaAllocator allocator,
                uint32_t maxVertices,
                uint32_t maxIndices,
                const std::string& name = "GpuMesh");
        ~GpuMesh() override;

        GpuMesh(const GpuMesh&) = delete;
        GpuMesh& operator=(const GpuMesh&) = delete;

        // --- Mesh interface ---
        VkBuffer GetVertexBuffer() const override { return vertexBuffer; }
        VkBuffer GetIndexBuffer()  const override { return indexBuffer; }
        uint32_t GetIndexCount()   const override;
        uint32_t GetVertexCount()  const override;

        // --- Compute shader interface ---
        /** Buffer that compute shaders write vertex data into (STORAGE + VERTEX) */
        VkBuffer GetVertexStorageBuffer() const { return vertexBuffer; }
        /** Buffer that compute shaders write index data into (STORAGE + INDEX) */
        VkBuffer GetIndexStorageBuffer()  const { return indexBuffer; }
        /** Buffer with two uint32: [vertexCount, indexCount]. Compute shaders atomicAdd to these. */
        VkBuffer GetCounterBuffer()       const { return counterBuffer; }

        uint32_t GetMaxVertices() const { return maxVertices; }
        uint32_t GetMaxIndices()  const { return maxIndices; }

        /**
         * Reset the atomic counters to zero. Call this before dispatching
         * the compute shader that fills this mesh.
         */
        void ResetCounters();

    private:
        VmaAllocator allocator;

        uint32_t maxVertices;
        uint32_t maxIndices;

        VkBuffer      vertexBuffer      = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation   = VK_NULL_HANDLE;

        VkBuffer      indexBuffer        = VK_NULL_HANDLE;
        VmaAllocation indexAllocation     = VK_NULL_HANDLE;

        // Host-visible counter buffer: [uint32 vertexCount, uint32 indexCount]
        VkBuffer      counterBuffer      = VK_NULL_HANDLE;
        VmaAllocation counterAllocation   = VK_NULL_HANDLE;
        uint32_t*     counterMappedPtr    = nullptr; // persistent mapping
    };
}
#endif //KRAKATOA_GPU_MESH_H
