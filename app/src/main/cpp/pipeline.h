#ifndef KRAKATOA_PIPELINE_H
#define KRAKATOA_PIPELINE_H

namespace graphics {
    class RenderPass;
    class Pipeline {
    public:
        Pipeline(RenderPass* renderPass);
        virtual ~Pipeline();
    protected:
        RenderPass* renderPass;
    };

    class PhongOpaquePipeline: public Pipeline {
    public:
        PhongOpaquePipeline(RenderPass* renderPass);
        ~PhongOpaquePipeline();
    };
}


#endif //KRAKATOA_PIPELINE_H
