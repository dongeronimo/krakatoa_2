#ifndef KRAKATOA_RDO_H
#define KRAKATOA_RDO_H
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <cassert>
namespace graphics {
    /**
     * The RDO takes the data from the upper layers of the app and bring it to the
     * pipeline.
     * */
    class RDO {
    public:
        enum Keys {
            MODEL_MAT, VIEW_MAT, PROJ_MAT, COLOR
        };
        void Add(Keys k, const glm::mat4& mat){
            mat4Table.insert({k, mat});
        }
        void Add(Keys k, const glm::vec4& v){
            vec4Table.insert({k, v});
        }
        glm::mat4& GetMat4(Keys k) {
            assert (mat4Table.count(k) > 0);
            return mat4Table[k];
        }
        glm::vec4& GetVec4(Keys k) {
            assert(vec4Table.count(k) > 0);
            return vec4Table[k];
        }
    private:
        std::unordered_map<Keys, glm::mat4> mat4Table;
        std::unordered_map<Keys, glm::vec4> vec4Table;
    };
}


#endif //KRAKATOA_RDO_H
