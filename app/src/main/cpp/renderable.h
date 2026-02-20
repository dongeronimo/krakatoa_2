#ifndef KRAKATOA_RENDERABLE_H
#define KRAKATOA_RENDERABLE_H
#include <string>
#include "transform.h"
#include "mesh.h"
namespace graphics {
    class StaticMesh;
    class Renderable {
    private:
        std::string meshId;
        const uint64_t id;
        Transform* transform;
        Mesh* mesh = nullptr;
    public:
        Transform& GetTransform(){return *transform;}
        uint64_t GetId()const{return id;}
        const std::string& GetMeshId()const{return meshId;}
        void SetMesh(Mesh* m){mesh = m;}
        Mesh* GetMesh()const{return mesh;}
        Renderable(const std::string& meshId);
        ~Renderable();
    };
}

#endif //KRAKATOA_RENDERABLE_H
