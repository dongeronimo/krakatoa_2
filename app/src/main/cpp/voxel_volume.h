#ifndef KRAKATOA_VOXEL_VOLUME_H
#define KRAKATOA_VOXEL_VOLUME_H
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <string>

namespace graphics {
    class CommandPoolManager;
    /**
     * A 1024³ R8_UINT 3D image used as an occupancy volume for voxelization.
     * Each voxel is 1 cm³. The centre of the volume (512,512,512) maps to world origin.
     * Created once, cleared to zero, and transitioned to GENERAL for compute storage use.
     */
    class VoxelVolume {
    public:
        static constexpr uint32_t VOLUME_SIZE = 1024;

        VoxelVolume(VkDevice device,
                    VmaAllocator allocator,
                    CommandPoolManager& cmdManager,
                    const std::string& name = "VoxelVolume");
        ~VoxelVolume();

        VoxelVolume(const VoxelVolume&) = delete;
        VoxelVolume& operator=(const VoxelVolume&) = delete;

        VkImage     GetImage()     const { return image; }
        VkImageView GetImageView() const { return imageView; }

    private:
        VkDevice     device;
        VmaAllocator allocator;

        VkImage       image      = VK_NULL_HANDLE;
        VkImageView   imageView  = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
    };
}
#endif //KRAKATOA_VOXEL_VOLUME_H
