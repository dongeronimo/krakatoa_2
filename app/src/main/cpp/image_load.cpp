#include "image_load.h"
#include "asset_loader.h"
#include <cassert>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
namespace io {
    void LoadImage(const std::string& path, std::vector<uint8_t>& output, VkFormat& format, int& width, int& height)
    {
        //TODO: Load the bytes using asset loader
        std::vector<uint8_t> bytes = io::AssetLoader::loadFile(path);
        int channelsInFile = -1;
        //TODO: Read with stb
        unsigned char* data = stbi_load_from_memory(bytes.data(),bytes.size(), &width, &height, &channelsInFile, 4);
        assert(channelsInFile != -1);
        output.clear();
        output.resize(width * height * channelsInFile);
        memcpy(output.data(),data, output.size());
        //TODO: Set the format
        switch (channelsInFile) {
            case 3:
                format = VK_FORMAT_R8G8B8_UNORM;
                break;
            case 4:
                format = VK_FORMAT_R8G8B8A8_UNORM;
                break;
        }
    }
}