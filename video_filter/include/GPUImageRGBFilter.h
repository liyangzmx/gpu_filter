//
// Created by liyang on 21-6-25.
//

#ifndef __GPU_IMAGE_RGB_FILTER_H__
#define __GPU_IMAGE_RGB_FILTER_H__

#include "GPUImageFilter.h"

class GPUImageRGBFilter : public GPUImageFilter {
public:
    GPUImageRGBFilter(float red, float green, float blue);
    virtual ~GPUImageRGBFilter();
    void setRed(float &_red);
    void setGreen(float &_green);
    void setBlue(float &_blue);
    virtual void onInit();
    virtual void onInitialized();

    static const char  *RGB_FRAGMENT_SHADER;
private:
    int redLocation;
    float red;
    int greenLocation;
    float green;
    int blueLocation;
    float blue;
};

#endif