//
// Created by liyang on 21-7-2.
//

#ifndef ANDROID_PRJ_GPUIMAGETWOPASSTEXTURESAMPLINGFILTER_H
#define ANDROID_PRJ_GPUIMAGETWOPASSTEXTURESAMPLINGFILTER_H

#include "GPUImageTwoPassFilter.h"

class GPUImageTwoPassTextureSamplingFilter : public GPUImageTwoPassFilter {
public:
    GPUImageTwoPassTextureSamplingFilter(
            const char *firstVertexShader,
            const char *firstFragmentShader,
            const char *secondVertexShader,
            const char *secondFragmentShader
    ) : GPUImageTwoPassFilter(firstVertexShader, firstFragmentShader, secondVertexShader, secondFragmentShader) {}

    virtual void onOutputSizeChanged(const int width, const int height) {
        GPUImageFilterGroup::onOutputSizeChanged(width, height);
        initTexelOffsets();
    }

    virtual void onInit() {
        GPUImageFilterGroup::onInit();
        initTexelOffsets();
    }

    virtual float getHorizontalTexelOffsetRatio() {
        return 1.0f;
    }

    virtual float getVerticalTexelOffsetRatio() {
        return 1.0f;
    }

protected:
    void initTexelOffsets() {
        float ratio = getHorizontalTexelOffsetRatio();
        GPUImageFilter *filter = getFilters()[0];
        int texelWidthOffsetLocation = glGetUniformLocation(filter->getProgram(), "texelWidthOffset");
        int texelHeightOffsetLocation = glGetUniformLocation(filter->getProgram(), "texelHeightOffset");
        filter->setFloat(texelWidthOffsetLocation, ratio / getOutputWidth());
        filter->setFloat(texelHeightOffsetLocation, 0.0f);

        ratio = getVerticalTexelOffsetRatio();
        filter = getFilters()[1];
        texelWidthOffsetLocation = glGetUniformLocation(filter->getProgram(), "texelWidthOffset");
        texelHeightOffsetLocation = glGetUniformLocation(filter->getProgram(), "texelHeightOffset");
        filter->setFloat(texelWidthOffsetLocation, 0.0f);
        filter->setFloat(texelHeightOffsetLocation, ratio / getOutputHeight());
    }
};


#endif //ANDROID_PRJ_GPUIMAGETWOPASSTEXTURESAMPLINGFILTER_H
