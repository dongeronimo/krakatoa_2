#include "static_mesh.h"
#include "command_pool_manager.h"
#include "vk_debug.h"
#include "android_log.h"
#include <cassert>
#include <cstring>
using namespace graphics;

StaticMesh::StaticMesh(VkDevice device,
                       VmaAllocator allocator,
                       CommandPoolManager& cmdManager,
                       const float* vertices,
                       uint32_t vertexCount,
                       const uint32_t* indices,
                       uint32_t indexCount,
                       const std::string& name)
        :
          allocator(allocator),
          vertexCount(vertexCount),
          indexCount(indexCount) {

    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(vertexCount) * 8 * sizeof(float);
    const VkDeviceSize indexSize = static_cast<VkDeviceSize>(indexCount) * sizeof(uint32_t);

    // --- Vertex buffer ---
    {
        // Staging buffer (host-visible)
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = vertexSize;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;
        VkResult result = vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                                          &stagingBuffer, &stagingAlloc, nullptr);
        assert(result == VK_SUCCESS);

        // Copy vertex data to staging
        void* mapped;
        vmaMapMemory(allocator, stagingAlloc, &mapped);
        memcpy(mapped, vertices, vertexSize);
        vmaUnmapMemory(allocator, stagingAlloc);

        // GPU-local vertex buffer
        VkBufferCreateInfo gpuInfo{};
        gpuInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        gpuInfo.size = vertexSize;
        gpuInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        gpuInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo gpuAllocInfo{};
        gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        result = vmaCreateBuffer(allocator, &gpuInfo, &gpuAllocInfo,
                                 &vertexBuffer, &vertexAllocation, nullptr);
        assert(result == VK_SUCCESS);
        if (!name.empty()) {
            debug::SetObjectName(device, vertexBuffer,
                                 Concatenate(name, ":VertexBuffer"));
        }

        // Upload via transfer queue with ownership transfer
        cmdManager.UploadBuffer(stagingBuffer, vertexBuffer, vertexSize,
                                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);

        // Destroy staging
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
    }

    // --- Index buffer ---
    {
        // Staging buffer (host-visible)
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = indexSize;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;
        VkResult result = vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                                          &stagingBuffer, &stagingAlloc, nullptr);
        assert(result == VK_SUCCESS);

        // Copy index data to staging
        void* mapped;
        vmaMapMemory(allocator, stagingAlloc, &mapped);
        memcpy(mapped, indices, indexSize);
        vmaUnmapMemory(allocator, stagingAlloc);

        // GPU-local index buffer
        VkBufferCreateInfo gpuInfo{};
        gpuInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        gpuInfo.size = indexSize;
        gpuInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        gpuInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo gpuAllocInfo{};
        gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        result = vmaCreateBuffer(allocator, &gpuInfo, &gpuAllocInfo,
                                 &indexBuffer, &indexAllocation, nullptr);
        assert(result == VK_SUCCESS);
        if (!name.empty()) {
            debug::SetObjectName(device, indexBuffer,
                                 Concatenate(name, ":IndexBuffer"));
        }

        // Upload via transfer queue with ownership transfer
        cmdManager.UploadBuffer(stagingBuffer, indexBuffer, indexSize,
                                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                VK_ACCESS_INDEX_READ_BIT);

        // Destroy staging
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
    }

    LOGI("StaticMesh created: %u vertices, %u indices (vb=%zu bytes, ib=%zu bytes)",
         vertexCount, indexCount, (size_t)vertexSize, (size_t)indexSize);
}

StaticMesh::~StaticMesh() {
    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indexBuffer, indexAllocation);
    }
    LOGI("StaticMesh destroyed");
}
