#ifndef KRAKATOA_MARCHING_CUBES_CONFIG_H
#define KRAKATOA_MARCHING_CUBES_CONFIG_H

#include "compute_pipeline.h"

namespace graphics {
    VkPipelineLayout MarchingCubesPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout);
    VkDescriptorSetLayout MarchingCubesDescriptorSetLayout(VkDevice device);
    /**
     * Creates the config for the marching cubes compute pipeline.
     * The dispatch callback reads the voxel volume and generates mesh geometry.
     */
    ComputePipelineConfig MarchingCubesConfig(VmaAllocator allocator);
}
#endif //KRAKATOA_MARCHING_CUBES_CONFIG_H
