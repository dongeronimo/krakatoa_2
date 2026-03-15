#ifndef KRAKATOA_VOXELIZATION_CONFIG_H
#define KRAKATOA_VOXELIZATION_CONFIG_H

#include "compute_pipeline.h"

namespace graphics {
    VkPipelineLayout VoxelizationPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout);
    VkDescriptorSetLayout VoxelizationDescriptorSetLayout(VkDevice device);
    /**
     * Creates the config for the voxelization compute pipeline.
     * The dispatch callback reads world-space positions from the deprojection output
     * and accumulates them into the 3D volume texture.
     */
    ComputePipelineConfig VoxelizationConfig();
}
#endif //KRAKATOA_VOXELIZATION_CONFIG_H
