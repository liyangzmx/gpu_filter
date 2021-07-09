//
// Created by liyang on 21-6-28.
//

#ifndef ANDROID_PRJ_PIXELBUFFER_H
#define ANDROID_PRJ_PIXELBUFFER_H

#include <iostream>
#include <vector>
#include <queue>
#include <mutex>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>
#include <thread>
#include <android/bitmap.h>

#include "glm.hpp"
#include "GLUtils.h"
#include "RenderImage.h"
#include "../video_filter/GPUImageFilter.h"
#include "../video_filter/GPUImageRenderer.h"

class PixelBuffer {
public:
    PixelBuffer(int width, int height);

    ~PixelBuffer();
    void setRenderer(GPUImageRenderer *renderer);
    void getRenderImage(RenderImage *dst);
    EGLConfig chooseConfig();
    void listConfig();
    int getConfigAttrib(EGLConfig config, int attrib);
    void getRenderImageWithFilterApplied(RenderImage *src, RenderImage *dst);
private:
    EGLDisplay eglDisplay;
    EGLConfig *eglConfigs;
    EGLConfig eglConfig;
    EGLContext eglContext;
    EGLSurface eglSurface;

    int m_Width = 0, m_Height = 0;
    GPUImageRenderer *m_Renderer;
    const bool LIST_CONFIGS = false;
    std::thread::id m_ThreadId;
    const char *m_ThreadOwner;
};


#endif //ANDROID_PRJ_PIXELBUFFER_H
