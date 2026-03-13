#ifndef KRAKATOA_CDO_H
#define KRAKATOA_CDO_H
#include <unordered_map>
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
            //data as uint16;
            uint16_buffer,
            //data as vec4
            vec4_buffer
        };


        //TODO deprojection: add the properties
    };
}
#endif //KRAKATOA_CDO_H
