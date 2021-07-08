//
// Created by liyang on 21-6-25.
//

#ifndef __GPU_IMAGE_FILTER_H__
#define __GPU_IMAGE_FILTER_H__

#include <vector>
#include <queue>
#include <mutex>
#include <iostream>
#include <GLES3/gl3.h>
#include "glm.hpp"
#include "GLUtils.h"
#include "../video_filter/GPUImageFilter.h"

class GPUImageFilter {
public:

    GPUImageFilter(bool isGroupFilter = false) :
        GPUImageFilter(NO_FILTER_VERTEX_SHADER, NO_FILTER_FRAGMENT_SHADER, isGroupFilter) {
        m_IsInitialized = false;
        m_IsGroupFilter = isGroupFilter;
    }
    GPUImageFilter(const char *vertexShader, const char *fragmentShader, bool isGroupFilter = false);
    virtual ~GPUImageFilter();
    virtual void onInit();
    virtual void onInitialized();
    virtual void onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer) ;
    virtual void onDrawArraysPre();
    virtual void onOutputSizeChanged(int width, int height);
    void ifNeedInit();
    bool isInitialized() const;
    int getOutputWidth();
    int getOutputHeight();
    GLuint getProgram();
    GLuint getAttribPosition();
    GLuint getAttribTextureCoordinate();
    GLuint UniformTexture();
    void setInteger(const int location, int intValue);
    void setFloat(const int location, float floatValue);
    void setFloatVec2(const int location, const float *arrayValue);
    void setFloatVec3(const int location, const float *arrayValue);
    void setFloatVec4(const int location, const float *arrayValue);
    void setFloatArray(const int location, const float *arrayValue, int length);
    void setFloatArray(const int location, const glm::vec2 value);
    void setUniformMatrix3f(const int location, const float *matrix);
    void setUniformMatrix4f(const int location, const float *matrix);
    bool isMIsGroupFilter() const;

    static const char *NO_FILTER_VERTEX_SHADER;
    static const char *NO_FILTER_FRAGMENT_SHADER;
protected:
    void runOnDraw(const std::function<void()> &T) {
        {
            std::lock_guard<std::mutex> guard(m_Lock);
            m_RunOnDraw.push(std::move(T));
        }
    }
    void runPendingOnDrawTasks() {
        {
            std::lock_guard<std::mutex> guard(m_Lock);
            while (!m_RunOnDraw.empty()) {
                std::function<void()> f = m_RunOnDraw.front();
                f();
                m_RunOnDraw.pop();
            }
        }
    }
    bool m_IsGroupFilter = false;

    GLuint m_ProgramId;
    GLuint m_AttribPosition;
    GLuint m_UniformTexture;
    GLuint m_AttribTextureCoordinate;
    bool m_IsInitialized;
    const char *m_VertexShader;
    const char *m_FragmentShader;

private:
    void init() {
        onInit();
        onInitialized();
    }
    std::queue<std::function<void()>>	m_RunOnDraw;
    std::mutex				m_Lock;
    int m_Width;
    int m_Height;
};

#endif //__GPU_IMAGE_FILTER_H__
