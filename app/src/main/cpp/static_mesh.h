#ifndef KRAKATOA_STATIC_MESH_H
#define KRAKATOA_STATIC_MESH_H
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <cstdint>
#include <vector>
namespace graphics {
    class CommandPoolManager;

    /**
     * GPU-resident static mesh. Holds Vulkan vertex and index buffers only;
     * CPU-side data is discarded after upload.
     *
     * Vertex format: px py pz nx ny nz u v (8 floats, 32 bytes per vertex).
     */
    class StaticMesh {
    public:
        /**
         * Create a static mesh and upload data to GPU.
         *
         * @param device         Logical device
         * @param allocator      VMA allocator
         * @param cmdManager     Command pool manager (for staging upload + queue ownership transfer)
         * @param vertices       Interleaved vertex data (px py pz nx ny nz u v)
         * @param vertexCount    Number of vertices
         * @param indices        Index data
         * @param indexCount     Number of indices
         */
        StaticMesh(
                   VmaAllocator allocator,
                   CommandPoolManager& cmdManager,
                   const float* vertices,
                   uint32_t vertexCount,
                   const uint32_t* indices,
                   uint32_t indexCount);

        ~StaticMesh();

        StaticMesh(const StaticMesh&) = delete;
        StaticMesh& operator=(const StaticMesh&) = delete;

        VkBuffer GetVertexBuffer() const { return vertexBuffer; }
        VkBuffer GetIndexBuffer() const { return indexBuffer; }
        uint32_t GetIndexCount() const { return indexCount; }
        uint32_t GetVertexCount() const { return vertexCount; }

    private:
        VmaAllocator allocator;

        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;

        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexAllocation = VK_NULL_HANDLE;

        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };
}
#endif //KRAKATOA_STATIC_MESH_H
