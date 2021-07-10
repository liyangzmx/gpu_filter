//
// Created by liyang on 21-7-5.
//

#ifndef ANDROID_PRJ_GPUIMAGENORMALBLENDFILTER_H
#define ANDROID_PRJ_GPUIMAGENORMALBLENDFILTER_H

#include "GPUImageTwoInputFilter.h"

class GPUImageNormalBlendFilter : public GPUImageTwoInputFilter {
public:
    static const char NORMAL_BLEND_FRAGMENT_SHADER[];
    GPUImageNormalBlendFilter();
};


#endif //ANDROID_PRJ_GPUIMAGENORMALBLENDFILTER_H
