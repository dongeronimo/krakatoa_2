#include "mutable_mesh.h"
#include "vk_mem_alloc.h"
#include "vk_debug.h"
#include "concatenate.h"
#include "android_log.h"
#include <cassert>
#include <cstring>

void graphics::MutableMesh::Advance() {
    AdvanceRingBuffers();
    UpdateCurrentSlotIfPending();
}

graphics::MutableMesh::MutableMesh(VkDevice device, VmaAllocator allocator,
                                   graphics::CommandPoolManager &cmdManager,
                                   uint32_t maxNumOfVerts,
                                   const std::string &name):
                                   maxNumOfVerts_(maxNumOfVerts),
                                   maxNumOfIndices_(maxNumOfVerts * 8),
                                   name(name), device(device), allocator(allocator){
    LOGI("MutableMesh '%s': pre-allocating %u max verts (%zu KB vtx, %zu KB idx) x %d slots",
         name.c_str(), maxNumOfVerts,
         (size_t)maxNumOfVerts * 8 * sizeof(float) / 1024,
         (size_t)maxNumOfIndices_ * sizeof(uint32_t) / 1024,
         MAX_FRAMES_IN_FLIGHT);

    for(auto i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vertexCount[i] = 0;
        indexCount[i] = 0;
        slotGeneration[i] = 0;
        AllocateSlotBuffers(i);
    }
}

graphics::MutableMesh::~MutableMesh() {
    for(auto i = 0; i < MAX_FRAMES_IN_FLIGHT; i++){
        if(vertexBuffer[i] != VK_NULL_HANDLE)
            vmaDestroyBuffer(allocator, vertexBuffer[i], vertexBufferAllocation[i]);
        if(indexBuffer[i] != VK_NULL_HANDLE)
            vmaDestroyBuffer(allocator, indexBuffer[i], indexBufferAllocation[i]);
    }
}

void graphics::MutableMesh::AllocateSlotBuffers(int slot) {
    // Vertex buffer: maxNumOfVerts * 8 floats (pos3 + norm3 + uv2)
    {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = static_cast<VkDeviceSize>(maxNumOfVerts_) * 8 * sizeof(float);
        bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                          | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo mapInfo{};
        vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                        &vertexBuffer[slot],
                        &vertexBufferAllocation[slot],
                        &mapInfo);
        vertexMappedPtr[slot] = mapInfo.pMappedData;
    }
    // Index buffer: maxNumOfVerts * 8 uint32_t
    {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = static_cast<VkDeviceSize>(maxNumOfIndices_) * sizeof(uint32_t);
        bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                          | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo mapInfo{};
        vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                        &indexBuffer[slot],
                        &indexBufferAllocation[slot],
                        &mapInfo);
        indexMappedPtr[slot] = mapInfo.pMappedData;
    }

    std::string vbName = Concatenate(name, " Vertex Buffer Slot #", slot);
    std::string ibName = Concatenate(name, " Index Buffer Slot #", slot);
    debug::SetBufferName(device, vertexBuffer[slot], vbName);
    debug::SetBufferName(device, indexBuffer[slot], ibName);
}

void graphics::MutableMesh::UpdateMesh(const float* verts, uint32_t vc,
                                       const uint32_t* idx, uint32_t ic) {
    assert(vc <= maxNumOfVerts_ && "UpdateMesh: vertex count exceeds maxNumOfVerts");
    assert(ic <= maxNumOfIndices_ && "UpdateMesh: index count exceeds maxNumOfIndices");

    size_t vertFloats = static_cast<size_t>(vc) * 8;
    size_t vertBytes = vertFloats * sizeof(float);
    size_t idxBytes = static_cast<size_t>(ic) * sizeof(uint32_t);

    bool same = (vertFloats == pendingVertices.size())
                && (ic == pendingIndices.size())
                && (memcmp(verts, pendingVertices.data(), vertBytes) == 0)
                && (memcmp(idx, pendingIndices.data(), idxBytes) == 0);

    if (same) return;

    pendingVertices.assign(verts, verts + vertFloats);
    pendingIndices.assign(idx, idx + ic);
    pendingGeneration++;
}

void graphics::MutableMesh::AdvanceRingBuffers() {
    vertexBuffer.Next();
    indexBuffer.Next();
    vertexBufferAllocation.Next();
    indexBufferAllocation.Next();
    vertexMappedPtr.Next();
    indexMappedPtr.Next();
    vertexCount.Next();
    indexCount.Next();
    slotGeneration.Next();
}

void graphics::MutableMesh::UpdateCurrentSlotIfPending() {
    if (slotGeneration.Current() < pendingGeneration) {
        UploadToCurrentSlot();
        slotGeneration.Current() = pendingGeneration;
    }
}

void graphics::MutableMesh::UploadToCurrentSlot() {
    // Just memcpy into the pre-allocated, persistently-mapped buffers
    size_t vertBytes = pendingVertices.size() * sizeof(float);
    size_t idxBytes = pendingIndices.size() * sizeof(uint32_t);

    memcpy(vertexMappedPtr.Current(), pendingVertices.data(), vertBytes);
    memcpy(indexMappedPtr.Current(), pendingIndices.data(), idxBytes);

    vertexCount.Current() = static_cast<uint32_t>(pendingVertices.size() / 8);
    indexCount.Current() = static_cast<uint32_t>(pendingIndices.size());
}
