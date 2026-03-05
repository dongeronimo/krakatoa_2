#ifndef KRAKATOA_RENDERABLE_H
#define KRAKATOA_RENDERABLE_H
#include <string>
#include "transform.h"
#include "mesh.h"
namespace graphics {
    class StaticMesh;
    class MutableMesh;
    class Renderable {
    private:
        std::string meshId;
        int64_t id;
        Transform* transform;
        Mesh* mesh = nullptr;
        bool ownsMesh = false;
    public:
        Transform& GetTransform(){return *transform;}
        uint64_t GetId()const{return id;}
        const std::string& GetMeshId()const{return meshId;}
        void SetMesh(Mesh* m, bool _ownsMesh = false){
            mesh = m;
            this->ownsMesh = _ownsMesh;
        }
        Mesh* GetMesh()const{return mesh;}
        Renderable(const std::string& meshId);
        Renderable(int16_t id);
        ~Renderable();
    };
}

#endif //KRAKATOA_RENDERABLE_H
