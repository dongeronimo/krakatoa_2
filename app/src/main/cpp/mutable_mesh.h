#ifndef KRAKATOA_MUTABLE_MESH_H
#define KRAKATOA_MUTABLE_MESH_H
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "mesh.h"
#include "ring_buffer.h"
namespace graphics {
    class CommandPoolManager;
    /**
     * Pre-allocated, ring-buffered mesh for streaming geometry every frame.
     *
     * Buffers are allocated ONCE at construction and reused via memcpy.
     * Vertex buffer size = maxNumOfVerts * 8 * sizeof(float)   (pos3 + norm3 + uv2)
     * Index buffer size  = maxNumOfVerts * 8 * sizeof(uint32_t)
     */
    class MutableMesh : public Mesh {
    public:
        static constexpr uint32_t DEFAULT_MAX_VERTS = 4096;
        static constexpr uint32_t DEFAULT_MAX_INDICES = DEFAULT_MAX_VERTS * 3;

        MutableMesh(VkDevice device, VmaAllocator allocator,
                    CommandPoolManager& cmdManager,
                    uint32_t maxNumOfVerts = DEFAULT_MAX_VERTS,
                    uint32_t maxNumOfIndices = DEFAULT_MAX_INDICES,
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
        uint32_t GetMaxNumOfVerts() const { return maxNumOfVerts_; }
        uint32_t GetMaxNumOfIndices() const { return maxNumOfIndices_; }
    private:
        uint32_t maxNumOfVerts_;
        uint32_t maxNumOfIndices_;
        /**One vertex buffer per frame*/
        utils::RingBuffer<VkBuffer> vertexBuffer;
        /**One index buffer per frame*/
        utils::RingBuffer<VkBuffer> indexBuffer;
        /**Each buffer needs it's own allocation.*/
        utils::RingBuffer<VmaAllocation> vertexBufferAllocation;
        /**Each buffer needs it's own allocation.*/
        utils::RingBuffer<VmaAllocation> indexBufferAllocation;
        /**Persistently mapped pointers — one per ring slot*/
        utils::RingBuffer<void*> vertexMappedPtr;
        utils::RingBuffer<void*> indexMappedPtr;
        /**One vertex count for each frame*/
        utils::RingBuffer<uint32_t> vertexCount;
        /**One index count per frame*/
        utils::RingBuffer<uint32_t> indexCount;
        /**generation tracks changes in the buffer, used to track which buffers need to be updated*/
        utils::RingBuffer<uint64_t> slotGeneration;
        /**I use the name to set names for vulkan objects in renderdoc*/
        const std::string name;
        VkDevice device;
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
        void AllocateSlotBuffers(int slot);
    };
}


#endif //KRAKATOA_MUTABLE_MESH_H
