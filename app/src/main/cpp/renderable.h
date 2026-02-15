#ifndef KRAKATOA_RENDERABLE_H
#define KRAKATOA_RENDERABLE_H
#include <string>
#include "transform.h"
namespace graphics {
    class Renderable {
    private:
        std::string meshId;
        const uint64_t id;
        Transform* transform;
    public:
        Transform& GetTransform(){return *transform;}
        uint64_t GetId()const{return id;}
        Renderable(const std::string& meshId);
        ~Renderable();
    };
}

#endif //KRAKATOA_RENDERABLE_H
