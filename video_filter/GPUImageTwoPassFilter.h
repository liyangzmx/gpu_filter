//
// Created by liyang on 21-7-2.
//

#ifndef ANDROID_PRJ_GPUIMAGETWOPASSFILTER_H
#define ANDROID_PRJ_GPUIMAGETWOPASSFILTER_H

#include "GPUImageFilterGroup.h"

class GPUImageTwoPassFilter : public GPUImageFilterGroup {
public:
    GPUImageTwoPassFilter(
            const char *firstVertexShader,
            const char *firstFragmentShader,
            const char *secondVertexShader,
            const char *secondFragmentShader
            ) : GPUImageFilterGroup() {
        addFilter(new GPUImageFilter(firstVertexShader, firstFragmentShader));
        addFilter(new GPUImageFilter(secondVertexShader, secondFragmentShader));
    }
};


#endif //ANDROID_PRJ_GPUIMAGETWOPASSFILTER_H
