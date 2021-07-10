//
// Created by liyang on 21-6-28.
//

#ifndef ANDROID_PRJ_GPUIMAGERENDERER_H
#define ANDROID_PRJ_GPUIMAGERENDERER_H

#include <vector>
#include <queue>
#include <mutex>
#include <GLES3/gl3.h>
#include <glm/detail/type_mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "RenderImage.h"
#include "GLUtils.h"
#include "GPUImageFilter.h"
#include "GPUImageFilterGroup.h"
#include "Rotation.h"
#include "ScaleType.h"
#include "GPUImageInputFilter.h"

using namespace glm;

#define TEXTURE_NUM 3
#define MATH_PI 3.1415926535897932384626433832802

class GPUImageRenderer {
public:
    GPUImageRenderer(GPUImageFilter *filter);
    ~GPUImageRenderer();
    void onDrawFrame();
    void onSurfaceChanged(int width, int height);
    void onSurfaceCreated();
    void setRenderImage(RenderImage *image);
    void setTexture(GLuint texture);
    void setFilter(GPUImageFilter *filter);
//    void deleteImage();
    void UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY);
private:
    void adjustImageScaling();
    void renderTexture();
    float addDistance(float coordinate, float distance);
    void runOnDraw(const std::function<void()> &T);
    void runOnDrawEnd(const std::function<void()> &T);
    void runAll(std::queue<std::function<void()>> &queue);

    GPUImageFilter *m_Filter = nullptr;
    std::queue<std::function<void()>>	m_RunOnDraw;
    std::queue<std::function<void()>>	m_RunOnDrawEnd;
    std::mutex				m_Lock;

    float m_BackgroundRed = 0;
    float m_BackgroundGreen = 0;
    float m_BackgroundBlue = 0;
    int outputWidth;
    int outputHeight;
    int imageWidth;
    int imageHeight;
    bool surfaceCreated = false;
    Rotation rotation;
    bool flipHorizontal = false;
    bool flipVertical = false;
    ScaleType scaleType = CENTER_CROP;

    const int NO_TEXTURE = -1;
    GLuint glTextureId = NO_TEXTURE;
    GLfloat glCubeBuffer[8] = {
            -1.0f, -1.0f,   // 1
            -1.0f, 1.0f,    // 0
            1.0f, -1.0f,    // 2
            1.0f, 1.0f,     // 3
    };

    GLfloat glTextureBuffer[8] = {
            0.0f, 1.0f,     // 1
            0.0f, 0.0f,     // 0
            1.0f, 1.0f,     // 2
            1.0f, 0.0f,     // 3
    };
    static float flip(const float i);

    glm::mat4 m_MVPMatrix;

    GPUImageInputFilter *m_GPUImageInputFilter = nullptr;
};


#endif //ANDROID_PRJ_GPUIMAGERENDERER_H
