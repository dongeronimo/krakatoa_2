#include "renderable.h"
#include <set>
#include <cassert>
static int64_t count = 0;
static std::set<int64_t> usedIds;
namespace graphics {

    Renderable::Renderable(const std::string &meshId):
        id(++count)
    {
        assert(usedIds.count(id) == 0);
        usedIds.insert(id);
        transform = new Transform(this);
    }

    Renderable::Renderable(int16_t _id) {
        assert(usedIds.count(_id) == 0);
        id = _id;
        meshId = "";
        transform = new Transform(this);
    }

    Renderable::~Renderable() {
        delete transform;
        if(ownsMesh)
            delete mesh;
    }
}