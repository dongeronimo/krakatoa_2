#include "mutable_mesh.h"
#include "vk_mem_alloc.h"
#include "vk_debug.h"
#include "concatenate.h"
void graphics::MutableMesh::Advance() {
    AdvanceRingBuffers();
    UpdateCurrentSlotIfPending();
}

graphics::MutableMesh::MutableMesh(VkDevice device, VmaAllocator allocator,
                                   graphics::CommandPoolManager &cmdManager,
                                   const std::string &name):
                                   name(name), device(device), allocator(allocator){
    for(auto i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
        vertexBuffer[i] = VK_NULL_HANDLE;
        indexBuffer[i] = VK_NULL_HANDLE;
        vertexBufferAllocation[i] = VK_NULL_HANDLE;
        indexBufferAllocation[i] = VK_NULL_HANDLE;
        vertexCount[i] = 0;
        slotGeneration[i] = 0;
    }
}

graphics::MutableMesh::~MutableMesh() {
    for(auto i=0; i<MAX_FRAMES_IN_FLIGHT; i++){
        if(vertexBuffer[i] != VK_NULL_HANDLE)
            vmaDestroyBuffer(allocator, vertexBuffer[i], vertexBufferAllocation[i]);
        if(indexBuffer[i] != VK_NULL_HANDLE)
            vmaDestroyBuffer(allocator, indexBuffer[i], indexBufferAllocation[i]);
    }
}

void graphics::MutableMesh::UpdateMesh(const float* verts, uint32_t vc,
                                       const uint32_t* idx, uint32_t ic) {
    size_t vertBytes = vc * 8 * sizeof(float);
    size_t idxBytes = ic * sizeof(uint32_t);

    bool same = (vc * 8 == pendingVertices.size())
                && (ic == pendingIndices.size())
                && (memcmp(verts, pendingVertices.data(), vertBytes) == 0)
                && (memcmp(idx, pendingIndices.data(), idxBytes) == 0);

    if (same) return;

    pendingVertices.assign(verts, verts + vc * 8);
    pendingIndices.assign(idx, idx + ic);
    pendingGeneration++;
}

void graphics::MutableMesh::AdvanceRingBuffers() {
    vertexBuffer.Next();
    indexBuffer.Next();
    vertexBufferAllocation.Next();
    indexBufferAllocation.Next();
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
    if(vertexBuffer.Current() != VK_NULL_HANDLE){
        vmaDestroyBuffer(allocator, vertexBuffer.Current(), vertexBufferAllocation.Current());
    }
    FillBuffer<float>(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               pendingVertices,
               vertexBufferAllocation.Current(),
               vertexBuffer.Current());
    FillBuffer<uint32_t>(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         pendingIndices,
                         indexBufferAllocation.Current(),
                         indexBuffer.Current());
    SetObjectsNames();

}
void graphics::MutableMesh::SetObjectsNames() {
    auto vb = vertexBuffer.Current();
    auto ib = indexBuffer.Current();
    std::string name1 = Concatenate(this->name, " Vertex Buffer Generation #",pendingGeneration);
    std::string name2 = Concatenate(this->name, " Index Buffer Generation #",pendingGeneration);
    debug::SetBufferName(device, vb, name1);
    debug::SetBufferName(device, ib, name2);
}
