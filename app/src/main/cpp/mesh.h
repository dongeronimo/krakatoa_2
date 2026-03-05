#ifndef KRAKATOA_MESH_H
#define KRAKATOA_MESH_H
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
namespace graphics {
    /**
     * Common mesh interface. It doesn't matter if we are dealing with skins,
     * mutable meshes or static meshes: the pipeline needs the vertex and index
     * buffers and their sizes.
     * */
    class Mesh {
    public:
        virtual ~Mesh() = default;
        virtual VkBuffer GetVertexBuffer()const = 0;
        virtual VkBuffer GetIndexBuffer()const =0;
        virtual uint32_t GetIndexCount()const = 0;
        virtual uint32_t GetVertexCount()const = 0;
    };
}
#endif //KRAKATOA_MESH_H
