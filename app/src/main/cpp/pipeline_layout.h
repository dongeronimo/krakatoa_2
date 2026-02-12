#ifndef KRAKATOA_PIPELINE_LAYOUT_H
#define KRAKATOA_PIPELINE_LAYOUT_H
#include <vulkan/vulkan.h>
#include <vector>
#include "android_log.h"
#include <cassert>
namespace graphics {

    /**
     * Builder for VkDescriptorSetLayout.
     * Usage:
     *   auto layout = DescriptorSetLayoutBuilder(device)
     *       .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
     *       .AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
     *       .Build();
     */
    class DescriptorSetLayoutBuilder {
    public:
        explicit DescriptorSetLayoutBuilder(VkDevice device) : device(device) {}

        DescriptorSetLayoutBuilder& AddBinding(uint32_t binding,
                                               VkDescriptorType type,
                                               VkShaderStageFlags stageFlags,
                                               uint32_t count = 1) {
            VkDescriptorSetLayoutBinding b{};
            b.binding = binding;
            b.descriptorType = type;
            b.descriptorCount = count;
            b.stageFlags = stageFlags;
            b.pImmutableSamplers = nullptr;
            bindings.push_back(b);
            return *this;
        }

        VkDescriptorSetLayout Build() {
            VkDescriptorSetLayoutCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            createInfo.pBindings = bindings.data();

            VkDescriptorSetLayout layout;
            VkResult result = vkCreateDescriptorSetLayout(device, &createInfo,
                                                          nullptr, &layout);
            assert(result == VK_SUCCESS);
            return layout;
        }

    private:
        VkDevice device;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
    };

    /**
     * Builder for VkPipelineLayout.
     * Usage:
     *   auto pipelineLayout = PipelineLayoutBuilder(device)
     *       .AddDescriptorSetLayout(descriptorSetLayout)
     *       .AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushData))
     *       .Build();
     */
    class PipelineLayoutBuilder {
    public:
        explicit PipelineLayoutBuilder(VkDevice device) : device(device) {}

        PipelineLayoutBuilder& AddDescriptorSetLayout(VkDescriptorSetLayout layout) {
            setLayouts.push_back(layout);
            return *this;
        }

        PipelineLayoutBuilder& AddPushConstantRange(VkShaderStageFlags stageFlags,
                                                    uint32_t offset,
                                                    uint32_t size) {
            VkPushConstantRange range{};
            range.stageFlags = stageFlags;
            range.offset = offset;
            range.size = size;
            pushConstantRanges.push_back(range);
            return *this;
        }

        VkPipelineLayout Build() {
            VkPipelineLayoutCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            createInfo.pSetLayouts = setLayouts.data();
            createInfo.pushConstantRangeCount =
                    static_cast<uint32_t>(pushConstantRanges.size());
            createInfo.pPushConstantRanges = pushConstantRanges.data();

            VkPipelineLayout layout;
            VkResult result = vkCreatePipelineLayout(device, &createInfo,
                                                     nullptr, &layout);
            assert(result == VK_SUCCESS);
            return layout;
        }

    private:
        VkDevice device;
        std::vector<VkDescriptorSetLayout> setLayouts;
        std::vector<VkPushConstantRange> pushConstantRanges;
    };
}
#endif //KRAKATOA_PIPELINE_LAYOUT_H