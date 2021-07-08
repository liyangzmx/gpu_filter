//
// Created by liyang on 21-6-25.
//

#ifndef ANDROID_PRJ_GPUIMAGEFILTERGROUP_H
#define ANDROID_PRJ_GPUIMAGEFILTERGROUP_H

#include <vector>
#include "GPUImageFilter.h"

class GPUImageFilterGroup : public GPUImageFilter {
public:
    GPUImageFilterGroup();
    ~GPUImageFilterGroup();
    GPUImageFilterGroup(const std::vector<GPUImageFilter *> filters);
    std::vector<GPUImageFilter *> &getMergedFilters();
    std::vector<GPUImageFilter *> &getFilters();
    void addFilter(GPUImageFilter *filter);
    virtual void onInit();
    virtual void onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer);
    virtual void onOutputSizeChanged(const int width, const int height);
    void updateMergedFilters();

    virtual void onInitialized();

private:
    void destroyFramebuffers();
    std::vector<GPUImageFilter *> m_Filters;
    std::vector<GPUImageFilter *> m_MergedFilters;
    int m_FramebuffersLen;
    int m_FramebufferTexturesLen;
    GLuint *m_Framebuffers;
    GLuint *m_FramebufferTextures;
};

#endif //ANDROID_PRJ_GPUIMAGEFILTERGROUP_H
