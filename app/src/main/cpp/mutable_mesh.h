#ifndef KRAKATOA_MUTABLE_MESH_H
#define KRAKATOA_MUTABLE_MESH_H
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "mesh.h"
#include "ring_buffer.h"
namespace graphics {
    class CommandPoolManager;
    class MutableMesh : public Mesh {
    public:
        MutableMesh(VkDevice device, VmaAllocator allocator, CommandPoolManager& cmdManager,
                    const std::string& name = "");
        ~MutableMesh();
        /**
         * Call this in the beginning of each frame.
         * */
        void Advance();
        VkBuffer GetVertexBuffer() const { return vertexBuffer.Current(); }
        VkBuffer GetIndexBuffer() const { return indexBuffer.Current(); }
        uint32_t GetIndexCount() const { return indexCount.Current(); }
        uint32_t GetVertexCount() const { return vertexCount.Current(); }
        void UpdateMesh(const float* vertices, uint32_t vertexCount,
                        const uint32_t* indices, uint32_t indexCount);
    private:
        /**One vertex buffer per frame*/
        utils::RingBuffer<VkBuffer> vertexBuffer;
        /**One index buffer per frame*/
        utils::RingBuffer<VkBuffer> indexBuffer;
        /**Each buffer needs it's own allocation.*/
        utils::RingBuffer<VmaAllocation> vertexBufferAllocation;
        /**Each buffer needs it's own allocation.*/
        utils::RingBuffer<VmaAllocation> indexBufferAllocation;
        /**One vertex count for each frame*/
        utils::RingBuffer<uint32_t> vertexCount;
        /**One index count per frame*/
        utils::RingBuffer<uint32_t> indexCount;
        /**generation tracks changes in the buffer, used to track which buffers need to be updated*/
        utils::RingBuffer<uint64_t> slotGeneration;
        /**I use the name to set names for vulkan objects in renderdoc*/
        const std::string name;
        /**We'll be creating buffers long since the object was instantiated.*/
        VkDevice device;
        /**We'll be creating buffers long since the object was instantiated.*/
        VmaAllocator allocator;
        /**Last vertex data*/
        std::vector<float> pendingVertices;
        /**Last index data*/
        std::vector<uint32_t> pendingIndices;
        /**Whenever we update the mesh we increase the generation.*/
        uint64_t pendingGeneration = 0;
        void AdvanceRingBuffers();
        void UpdateCurrentSlotIfPending();
        void UploadToCurrentSlot();
        template <typename t>
        void FillBuffer(VkBufferUsageFlags usage,
                        const std::vector<t>& data,
                        VmaAllocation& allocation,
                        VkBuffer& buffer){
            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = data.size() * sizeof(t);
            bufInfo.usage = usage;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                              | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo mapInfo;
            vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                            &buffer,
                            &allocation,
                            &mapInfo);

            memcpy(mapInfo.pMappedData, data.data(), data.size() * sizeof(t));
        }
        void SetObjectsNames();
    };
}


#endif //KRAKATOA_MUTABLE_MESH_H
