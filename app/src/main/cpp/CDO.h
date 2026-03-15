#ifndef KRAKATOA_CDO_H
#define KRAKATOA_CDO_H
#include <unordered_map>
#include <vector>
#include <array>
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
            vec4_buffer,
            //view inverse matrix
            view_inverse,
            //3d volume image view for voxelization
            volume_image_view,
            //position count for voxelization dispatch
            position_count,
            //voxel scale factor (meters to voxel units, e.g. 100.0 for 1cm)
            voxel_scale,
            //marching cubes cutoff threshold (0-255)
            mc_cutoff,
            //marching cubes max discontinuity distance (in voxel units)
            mc_max_distance,
            //marching cubes vertex output buffer
            mc_vertex_buffer,
            //marching cubes index output buffer
            mc_index_buffer,
            //marching cubes atomic counter buffer
            mc_counter_buffer,
            //marching cubes max vertices capacity
            mc_max_vertices,
            //marching cubes max indices capacity
            mc_max_indices
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
        void Add(Keys k, VkBuffer v){
            vkBufferTable.insert({k, v});
        }
        void Add(Keys k, VkImageView v){
            vkImageViewTable.insert({k, v});
        }
        void Add(Keys k, uint32_t v){
            uint32Table.insert({k, v});
        }
        void Add(Keys k, const std::array<float,16>& v){
            mat4Table.insert({k, v});
        }
        float GetFloat(Keys k) const {
            return floatTable.at(k);
        }
        int32_t GetInt32(Keys k) const {
            return int32Table.at(k);
        }
        VkBuffer GetVkBuffer(Keys k) const {
            return vkBufferTable.at(k);
        }
        const std::array<float,16>& GetMat4(Keys k) const {
            return mat4Table.at(k);
        }
        VkImageView GetVkImageView(Keys k) const {
            return vkImageViewTable.at(k);
        }
        uint32_t GetUint32(Keys k) const {
            return uint32Table.at(k);
        }
    private:
        std::unordered_map<Keys, float> floatTable;
        std::unordered_map<Keys, std::vector<uint16_t>> vectorUint16Table;
        std::unordered_map<Keys, VkBuffer> vkBufferTable;
        std::unordered_map<Keys, int32_t> int32Table;
        std::unordered_map<Keys, std::array<float,16>> mat4Table;
        std::unordered_map<Keys, VkImageView> vkImageViewTable;
        std::unordered_map<Keys, uint32_t> uint32Table;
    };
}
#endif //KRAKATOA_CDO_H
