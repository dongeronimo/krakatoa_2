#ifndef KRAKATOA_TSDF_VOLUME_H
#define KRAKATOA_TSDF_VOLUME_H
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <string>

namespace graphics {
    class CommandPoolManager;
    /**
     * A 256³ R32_UINT 3D image used as a TSDF (Truncated Signed Distance Function) volume.
     *
     * Each voxel is packed into a single uint32:
     *   bits [31:16] = TSDF distance as biased int16 (maps [-1,1] to [0,65535])
     *   bits [15:0]  = weight as uint16
     *
     * Initial state: TSDF = 1.0 (free space), weight = 0.
     * Sign convention: positive = free space, zero = surface, negative = inside object.
     *
     * The center of the volume (VOLUME_SIZE/2) maps to the world origin.
     * Voxel size is determined by the scale parameter: voxelSize = 1/scale meters.
     */
    class TsdfVolume {
    public:
        static constexpr uint32_t VOLUME_SIZE = 256;

        /// Packed value for (tsdf=1.0, weight=0): biased distance = 65535, weight = 0
        static constexpr uint32_t INITIAL_PACKED_VALUE = 0xFFFF0000u;

        TsdfVolume(VkDevice device,
                   VmaAllocator allocator,
                   CommandPoolManager& cmdManager,
                   const std::string& name = "TsdfVolume");
        ~TsdfVolume();

        TsdfVolume(const TsdfVolume&) = delete;
        TsdfVolume& operator=(const TsdfVolume&) = delete;

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
#endif //KRAKATOA_TSDF_VOLUME_H
