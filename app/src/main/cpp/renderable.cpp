#include "renderable.h"
static uint64_t count = 0;
namespace graphics {

    Renderable::Renderable(const std::string &meshId):
        id(++count){
        transform = new Transform(this);
    }

    Renderable::~Renderable() {
        delete transform;
    }
}