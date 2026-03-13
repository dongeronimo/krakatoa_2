#ifndef KRAKATOA_DEPTH_DEPROJECTION_CONFIG_H
#define KRAKATOA_DEPTH_DEPROJECTION_CONFIG_H

#include "compute_pipeline.h"
#include <vector>
#include "ring_buffer.h"
namespace graphics {
    VkPipelineLayout  DepthDeprojectionPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout);
    VkDescriptorSetLayout DepthDeprojectionDescriptorSetLayout(VkDevice device);
    /**
     * Creates the config for the depth deprojection compute pipeline.
     * */
    ComputePipelineConfig DepthDeprojectConfig();

    struct DepthDeprojectionOutput {
        utils::RingBuffer<VkBuffer> outputBuffer;
        utils::RingBuffer<VmaAllocation> outputBufferAllocation;
        utils::RingBuffer<size_t> outputBufferSize;
    };
}
#endif //KRAKATOA_DEPTH_DEPROJECTION_CONFIG_H
