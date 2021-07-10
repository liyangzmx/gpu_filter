//
// Created by liyang on 21-7-5.
//

#ifndef ANDROID_PRJ_GPUIMAGEBILATERALBLURFILTER_H
#define ANDROID_PRJ_GPUIMAGEBILATERALBLURFILTER_H

#include "GPUImageFilter.h"

class GPUImageBilateralBlurFilter : public GPUImageFilter {
public:
    static const char *BILATERAL_VERTEX_SHADER;
    static const char *BILATERAL_FRAGMENT_SHADER;

    GPUImageBilateralBlurFilter() {
        GPUImageBilateralBlurFilter(1.0);
    }

    GPUImageBilateralBlurFilter(float _distanceNormalizationFactor) : GPUImageFilter(BILATERAL_VERTEX_SHADER, BILATERAL_FRAGMENT_SHADER) {
        distanceNormalizationFactor = _distanceNormalizationFactor;
    }

    virtual void onInit();
    virtual void onInitialized();

private:
    virtual void onOutputSizeChanged(int width, int height);

private:
    void setDistanceNormalizationFactor(const GLfloat newValue);
    void setTexelSize(const int width, const int height);
    float whVal[2];
    GLfloat distanceNormalizationFactor;
    GLuint disFactorLocation;
    GLuint singleStepOffsetLocation;
};


#endif //ANDROID_PRJ_GPUIMAGEBILATERALBLURFILTER_H
