//
// Created by liyang on 21-7-2.
//

#ifndef ANDROID_PRJ_GPUIMAGEGAUSSIANBLURFILTER_H
#define ANDROID_PRJ_GPUIMAGEGAUSSIANBLURFILTER_H

#include "GPUImageTwoPassTextureSamplingFilter.h"

class GPUImageGaussianBlurFilter : public GPUImageTwoPassTextureSamplingFilter {
public:
    static const char *VERTEX_SHADER;

    static const char *FRAGMENT_SHADER;

    GPUImageGaussianBlurFilter(float blurSize = 6.0)
            :GPUImageTwoPassTextureSamplingFilter(VERTEX_SHADER, FRAGMENT_SHADER, VERTEX_SHADER, FRAGMENT_SHADER),
            m_BlurSize(blurSize)
            {}

    virtual void onInitialized() {
        GPUImageFilterGroup::onInitialized();
        setBlurSize(m_BlurSize);
    }

    virtual float getHorizontalTexelOffsetRatio() {
        return m_BlurSize * 1.75;
    }

    virtual float getVerticalTexelOffsetRatio() {
        return m_BlurSize;
    }

    void setBlurSize(float blurSize) {
        m_BlurSize = blurSize;
        runOnDraw([this](){
            initTexelOffsets();
        });
    }

protected:
    float m_BlurSize;
};


#endif //ANDROID_PRJ_GPUIMAGEGAUSSIANBLURFILTER_H
