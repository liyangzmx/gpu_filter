//
// Created by liyang on 21-6-28.
//

#include <math.h>
#include "TextureRotationUtil.h"
#include "GPUImageRenderer.h"

GPUImageRenderer::GPUImageRenderer(GPUImageFilter *filter) :
    glTextureId(NO_TEXTURE), imageWidth(0), imageHeight(0){
    if(filter != nullptr) {
        m_GPUImageInputFilter = new GPUImageInputFilter();
        GPUImageFilterGroup *filterGroup = new GPUImageFilterGroup();
        filterGroup->addFilter((GPUImageFilter *)m_GPUImageInputFilter);
        filterGroup->addFilter(filter);
        m_Filter = (GPUImageFilter *)filterGroup;
    } else {
        m_GPUImageInputFilter = new GPUImageInputFilter();
        m_Filter = (GPUImageFilter *)m_GPUImageInputFilter;
    }
    UpdateMVPMatrix(0, 0, 1.0f, 1.0f);
}

GPUImageRenderer::~GPUImageRenderer() {
    if(m_Filter) {
        delete m_Filter;
        m_Filter = nullptr;
    }
}

void GPUImageRenderer::UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY)
{
    angleX = angleX % 360;
    angleY = angleY % 360;

    //转化为弧度角
    float radiansX = static_cast<float>(MATH_PI / 180.0f * angleX);
    float radiansY = static_cast<float>(MATH_PI / 180.0f * angleY);
    // Projection matrix
    glm::mat4 Projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    //video_filter.glm::mat4 Projection = video_filter.glm::frustum(-ratio, ratio, -1.0f, 1.0f, 4.0f, 100.0f);
    //video_filter.glm::mat4 Projection = video_filter.glm::perspective(45.0f,ratio, 0.1f,100.f);

    // View matrix
    glm::mat4 View = glm::lookAt(
            glm::vec3(0, 0, 4), // Camera is at (0,0,1), in World Space
            glm::vec3(0, 0, 0), // and looks at the origin
            glm::vec3(0, 1, 0)  // Head is up (set to 0,-1,0 to look upside-down)
    );

    // Model matrix
    glm::mat4 Model = glm::mat4(1.0f);
    Model = glm::scale(Model, glm::vec3(scaleX, scaleY, 1.0f));
    Model = glm::rotate(Model, radiansX, glm::vec3(1.0f, 0.0f, 0.0f));
    Model = glm::rotate(Model, radiansY, glm::vec3(0.0f, 1.0f, 0.0f));
    Model = glm::translate(Model, glm::vec3(0.0f, 0.0f, 0.0f));

    m_MVPMatrix = Projection * View * Model;
}

void GPUImageRenderer::runOnDraw(const std::function<void()> &T) {
    {
        std::lock_guard <std::mutex> guard(m_Lock);
        m_RunOnDraw.push(std::move(T));
    }
}

void GPUImageRenderer::runOnDrawEnd(const std::function<void()> &T) {
    {
        std::lock_guard <std::mutex> guard(m_Lock);
        m_RunOnDrawEnd.push(std::move(T));
    }
}

void GPUImageRenderer::runAll(std::queue <std::function<void()>> &queue) {
    {
        std::lock_guard <std::mutex> guard(m_Lock);
        while (!queue.empty()) {
            std::function<void()> f = m_RunOnDraw.front();
            f();
            queue.pop();
        }
    }
}

void GPUImageRenderer::onSurfaceCreated() {
    surfaceCreated = true;
    glClearColor(m_BackgroundRed, m_BackgroundGreen, m_BackgroundBlue, 1);
    glDisable(GL_DEPTH_TEST);

    if(m_Filter) {
        m_Filter->ifNeedInit();
    }
}

void GPUImageRenderer::setRenderImage(RenderImage *image) {
    if (imageWidth != image->width) {
        imageWidth = image->width;
        imageHeight = image->height;
        adjustImageScaling();
    }
    if(m_GPUImageInputFilter != nullptr) {
        m_GPUImageInputFilter->setRenderImage(image);
    }
}

float GPUImageRenderer::addDistance(float coordinate, float distance) {
    return coordinate == 0.0f ? distance : 1 - distance;
}

void GPUImageRenderer::adjustImageScaling() {
    if((imageWidth == 0) || (imageHeight == 0)) {
        return;
    }
    int out_w = outputWidth;
    int out_h = outputHeight;
    if (rotation == ROTATION_270 || rotation == ROTATION_90) {
        out_w = outputHeight;
        out_h = outputWidth;
    }

    float ratio1 = 1.0f * out_w / imageWidth;
    float ratio2 = 1.0f * out_h / imageHeight;
    float ratioMax = std::max(ratio1, ratio2);

    int imageWidthNew = std::round(imageWidth * ratioMax);
    int imageHeightNew = std::round(imageHeight * ratioMax);

    float ratioWidth = 1.0f * imageWidthNew / outputWidth;
    float ratioHeight = 1.0f * imageHeightNew / outputHeight;

    switch (rotation) {
        case ROTATION_90:
            for(int i = 0; i < 8; i++)
                glTextureBuffer[i] = TextureRotationUtil::TEXTURE_ROTATED_90[i];
            break;
        case ROTATION_180:
            for(int i = 0; i < 8; i++)
                glTextureBuffer[i] = TextureRotationUtil::TEXTURE_ROTATED_180[i];
            break;
        case ROTATION_270:
            for(int i = 0; i < 8; i++)
                glTextureBuffer[i] = TextureRotationUtil::TEXTURE_ROTATED_270[i];
            break;
        case NORMAL:
        default:
            for(int i = 0; i < 8; i++)
                glTextureBuffer[i] = TextureRotationUtil::TEXTURE_NO_ROTATION[i];
            break;
    }
    if (scaleType == CENTER_CROP) {
        float distHorizontal = (1 - 1.0 / ratioWidth) / 2;
        float distVertical = (1 - 1.0 / ratioHeight) / 2;

        for (int i = 0; i < 8; ++i) {
            if((i % 2) == 0) {
                glTextureBuffer[i] = glTextureBuffer[i] == 0.0f ? distHorizontal : 1.0f - distHorizontal;
            } else {
                glTextureBuffer[i] = glTextureBuffer[i] == 0.0f ? distVertical : 1.0f - distVertical;
            }
        }
    }
    for (int i = 0; i < 8; ++i) {
        if((i % 2) == 0) {
            glCubeBuffer[i] = TextureRotationUtil::CUBE[i] / ratioWidth;
        } else {
            glCubeBuffer[i] = TextureRotationUtil::CUBE[i] / ratioHeight;
        }
    }
    if (flipHorizontal) {
        for (int i = 0; i < 8; ++i) {
            if((i % 2) == 0) {
                glTextureBuffer[i] = flip(glTextureBuffer[i]);
            }
        }
    }
    if (flipVertical) {
        for (int i = 0; i < 8; ++i) {
            if((i % 2) == 1) {
                glTextureBuffer[i] = flip(glTextureBuffer[i]);
            }
        }
    }
}

float GPUImageRenderer::flip(const float i) {
    if (i == 0.0f) {
        return 1.0f;
    }
    return 0.0f;
}

void GPUImageRenderer::onDrawFrame() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if(!surfaceCreated)
        return;
    runAll(m_RunOnDraw);
    if(m_Filter != nullptr) {
        m_Filter->onDraw(glTextureId, glCubeBuffer, glTextureBuffer);
    }
    runAll(m_RunOnDrawEnd);
}

void GPUImageRenderer::onSurfaceChanged(int width, int height) {
    outputWidth = width;
    outputHeight = height;

    glViewport(0, 0, width, height);
    if(m_Filter != nullptr)
        m_Filter->onOutputSizeChanged(width, height);
}

void GPUImageRenderer::setFilter(GPUImageFilter *filter) {
    runOnDraw([this, filter](){
        GPUImageFilter *oldFilter = m_Filter;
        m_Filter = filter;
        if(oldFilter != nullptr) {
            delete oldFilter;
        }
        m_Filter->ifNeedInit();
        glUseProgram(m_Filter->getProgram());
        m_Filter->onOutputSizeChanged(outputWidth, outputHeight);
    });
}

void GPUImageRenderer::setTexture(GLuint texture) {
    glTextureId = texture;
}
