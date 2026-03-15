#include "gpu_mesh.h"
#include "vk_debug.h"
#include "concatenate.h"
#include "android_log.h"
#include <cassert>
#include <cstring>

using namespace graphics;

GpuMesh::GpuMesh(VkDevice device,
                 VmaAllocator allocator,
                 uint32_t maxVertices,
                 uint32_t maxIndices,
                 const std::string& name)
    : device(device), allocator(allocator),
      maxVertices(maxVertices), maxIndices(maxIndices)
{
    // --- Vertex buffer (GPU-local, used as both storage and vertex buffer) ---
    {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size  = static_cast<VkDeviceSize>(maxVertices) * BYTES_PER_VERTEX;
        bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult r = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                     &vertexBuffer, &vertexAllocation, nullptr);
        assert(r == VK_SUCCESS);
        debug::SetBufferName(device, vertexBuffer, Concatenate(name, ":VertexBuffer"));
    }

    // --- Index buffer (GPU-local, used as both storage and index buffer) ---
    {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size  = static_cast<VkDeviceSize>(maxIndices) * sizeof(uint32_t);
        bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult r = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                     &indexBuffer, &indexAllocation, nullptr);
        assert(r == VK_SUCCESS);
        debug::SetBufferName(device, indexBuffer, Concatenate(name, ":IndexBuffer"));
    }

    // --- Counter buffer (host-visible + device-visible for atomic ops + CPU readback) ---
    {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size  = sizeof(uint32_t) * 2; // [vertexCount, indexCount]
        bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                        | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VmaAllocationInfo mapInfo{};
        VkResult r = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                     &counterBuffer, &counterAllocation, &mapInfo);
        assert(r == VK_SUCCESS);
        counterMappedPtr = static_cast<uint32_t*>(mapInfo.pMappedData);
        assert(counterMappedPtr != nullptr);

        // Initialize to zero
        counterMappedPtr[0] = 0;
        counterMappedPtr[1] = 0;

        debug::SetBufferName(device, counterBuffer, Concatenate(name, ":CounterBuffer"));
    }

    LOGI("GpuMesh created: maxVerts=%u maxIdx=%u name='%s'", maxVertices, maxIndices, name.c_str());
}

GpuMesh::~GpuMesh() {
    if (vertexBuffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
    if (indexBuffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, indexBuffer, indexAllocation);
    if (counterBuffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, counterBuffer, counterAllocation);
    LOGI("GpuMesh destroyed");
}

uint32_t GpuMesh::GetVertexCount() const {
    // Read from the host-mapped counter buffer (written by compute shader atomics)
    return counterMappedPtr[0];
}

uint32_t GpuMesh::GetIndexCount() const {
    return counterMappedPtr[1];
}

void GpuMesh::ResetCounters() {
    counterMappedPtr[0] = 0;
    counterMappedPtr[1] = 0;
}
