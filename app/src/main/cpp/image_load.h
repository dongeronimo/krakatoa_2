#ifndef KRAKATOA_IMAGE_LOAD_H
#define KRAKATOA_IMAGE_LOAD_H
#include <vector>
#include <string>
#include <vulkan/vulkan.h>
namespace io {
    /**
     * Loads an image and returns it as a vector of bytes.
     * It uses the asset loader so the asset loader has to be initialized before calling this function.
     * I know it supports PNG. Maybe it supports JPG too (i'm using STB to read the file)
     * */
    void LoadImage(const std::string& path, std::vector<uint8_t>& data, VkFormat& format, int& width, int& height);
}

#endif //KRAKATOA_IMAGE_LOAD_H
