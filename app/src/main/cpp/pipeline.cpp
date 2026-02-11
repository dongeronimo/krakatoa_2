#include "pipeline.h"
using namespace graphics;

Pipeline::Pipeline(RenderPass *renderPass) :
renderPass(renderPass){

}

Pipeline::~Pipeline() {

}

PhongOpaquePipeline::PhongOpaquePipeline(RenderPass *renderPass) : Pipeline(renderPass) {

}

PhongOpaquePipeline::~PhongOpaquePipeline() {

}
