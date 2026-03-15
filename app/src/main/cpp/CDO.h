#ifndef KRAKATOA_CDO_H
#define KRAKATOA_CDO_H
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>
namespace graphics {
    class CDO {
    public:
        //TODO deprojection (done): Define the enums
        enum Keys {
            //Focal length in px
            fx,
            //Focal length in px
            fy,
            //Principal point
            cx,
            //Principal point
            cy,
            //width
            width,
            //height
            height,
            //data as uint16;
            uint16_buffer,
            //data as vec4
            vec4_buffer
        };
        void Add(Keys k, float v){
            floatTable.insert({k, v});
        }
        void Add(Keys k, int32_t v){
            int32Table.insert({k,v});
        }
        void Add(Keys k, std::vector<uint16_t>& v){
            vectorUint16Table.insert({k, v});
        }
    private:
        std::unordered_map<Keys, float> floatTable;
        std::unordered_map<Keys, std::vector<uint16_t>> vectorUint16Table;
        std::unordered_map<Keys, VkBuffer> vkBufferTable;
        std::unordered_map<Keys, int32_t> int32Table
    };
}
#endif //KRAKATOA_CDO_H
