//
// Created by liyang on 21-7-5.
//

#ifndef ANDROID_PRJ_GPUIMAGESHARPENFILTER_H
#define ANDROID_PRJ_GPUIMAGESHARPENFILTER_H

#include "GPUImageFilter.h"

class GPUImageSharpenFilter : public GPUImageFilter {
public:
    GPUImageSharpenFilter() {
        GPUImageSharpenFilter(1.0);
    }
    GPUImageSharpenFilter(float _sharpness) : GPUImageFilter(SHARPEN_VERTEX_SHADER, SHARPEN_FRAGMENT_SHADER) {
        sharpness = _sharpness;
    }
    static const char *SHARPEN_VERTEX_SHADER;
    static const char *SHARPEN_FRAGMENT_SHADER;

    virtual void onInit();
    virtual void onInitialized();

    virtual void onOutputSizeChanged(int width, int height);

    void setSharpness(const GLfloat _sharpness);

private:
    GLuint sharpnessLocation;
    GLfloat sharpness;
    GLuint imageWidthFactorLocation;
    GLuint imageHeightFactorLocation;
};


#endif //ANDROID_PRJ_GPUIMAGESHARPENFILTER_H
