#ifndef KRAKATOA_TEXTURE2D_H
#define KRAKATOA_TEXTURE2D_H
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>
#include <cstdint>
#include <string>
namespace graphics {
    class CommandPoolManager;

    /**
     * GPU-resident 2D texture. Holds a Vulkan image, image view and metadata.
     * CPU-side pixel data is discarded after upload.
     *
     * The image is uploaded via a staging buffer and ends in
     * VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ready for sampling.
     */
    class Texture2D {
    public:
        /**
         * Create a 2D texture and upload pixel data to the GPU.
         *
         * @param device      Logical device (for image view creation and debug naming)
         * @param allocator   VMA allocator
         * @param cmdManager  Command pool manager (for staging upload + queue ownership transfer)
         * @param pixels      Raw pixel data matching the given format
         * @param width       Image width in texels
         * @param height      Image height in texels
         * @param format      Vulkan format (e.g. VK_FORMAT_R8G8B8A8_UNORM)
         * @param name        Debug name for this texture
         */
        Texture2D(VkDevice device,
                  VmaAllocator allocator,
                  CommandPoolManager& cmdManager,
                  const std::vector<uint8_t>& pixels,
                  uint32_t width,
                  uint32_t height,
                  VkFormat format,
                  const std::string& name = "");

        ~Texture2D();

        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;

        VkImage     GetImage()     const { return image; }
        VkImageView GetImageView() const { return imageView; }
        VkFormat    GetFormat()    const { return format; }
        uint32_t    GetWidth()     const { return width; }
        uint32_t    GetHeight()    const { return height; }

    private:
        VkDevice     device;
        VmaAllocator allocator;

        VkImage       image      = VK_NULL_HANDLE;
        VkImageView   imageView  = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkFormat format;
        uint32_t width  = 0;
        uint32_t height = 0;
    };
}
#endif //KRAKATOA_TEXTURE2D_H
