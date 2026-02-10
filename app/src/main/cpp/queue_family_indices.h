// queue_family_indices.h
#ifndef KRAKATOA_QUEUE_FAMILY_INDICES_H
#define KRAKATOA_QUEUE_FAMILY_INDICES_H

#include <optional>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;
    std::optional<uint32_t> transferFamily;

    // Indices dentro das familias
    uint32_t graphicsQueueIndex = 0;
    uint32_t presentQueueIndex = 0;
    uint32_t computeQueueIndex = 0;
    uint32_t transferQueueIndex = 0;

    bool isComplete() const {
        return graphicsFamily.has_value() &&
               presentFamily.has_value() &&
               computeFamily.has_value() &&
               transferFamily.has_value();
    }
};

#endif