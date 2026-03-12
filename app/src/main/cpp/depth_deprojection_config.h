#ifndef KRAKATOA_DEPTH_DEPROJECTION_CONFIG_H
#define KRAKATOA_DEPTH_DEPROJECTION_CONFIG_H

#include "compute_pipeline.h"
#include <vector>
namespace graphics {
    VkPipelineLayout  DepthDeprojectionPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout);
    VkDescriptorSetLayout DepthDeprojectionDescriptorSetLayout(VkDevice device);
    /**
     * Creates the config for the depth deprojection compute pipeline.
     * */
    ComputePipelineConfig DepthDeprojectConfig(VkBuffer intrinsicsUBO,
                                               VkBuffer depthSSBO,
                                               VkBuffer outputSSBO,
                                               uint32_t width, uint32_t height);
}
#endif //KRAKATOA_DEPTH_DEPROJECTION_CONFIG_H
