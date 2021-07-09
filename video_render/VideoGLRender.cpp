#include "VideoGLRender.h"
#include "../video_filter/GLUtils.h"
#include <gtc/matrix_transform.hpp>
#include "../video_filter/PixelBuffer.h"
#include "../video_filter/GPUImageFilter.h"
#include "../video_filter/GPUImageRGBFilter.h"
#include "../video_filter/GPUImageTextFilter.h"
#include "../video_filter/GPUImageRenderer.h"
#include "../video_filter/GPUImageFilterGroup.h"
#include "../video_filter/GPUImageGaussianBlurFilter.h"
#include "../video_filter/GPUImageSharpenFilter.h"
#include "../video_filter/GPUImageBilateralBlurFilter.h"
#include "../video_filter/GPUImageTwoInputFilter.h"
#include "../video_filter/GPUImageNormalBlendFilter.h"

#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>

VideoGLRender* VideoGLRender::s_Instance = nullptr;
std::mutex VideoGLRender::m_Mutex;

VideoGLRender::VideoGLRender(){
    m_InitDone = false;

#define __USE_PIXEL_BUFFER__
#ifdef __USE_PIXEL_BUFFER__
    m_GPUImageRenderer = new GPUImageRenderer(nullptr);
#else
    GPUImageFilterGroup *filterGroup = new GPUImageFilterGroup();
    m_GPUImageTextRender = new GPUImageTextFilter();
    filterGroup->addFilter(new GPUImageRGBFilter(1.0f, 0.9f, 0.9f));
    filterGroup->addFilter(new GPUImageRGBFilter(1.0f, 0.9f, 0.9f));
    filterGroup->addFilter(new GPUImageRGBFilter(1.0f, 0.9f, 0.9f));
    filterGroup->addFilter(new GPUImageRGBFilter(1.0f, 0.9f, 0.9f));
    filterGroup->addFilter(new GPUImageGaussianBlurFilter(0.2f));
    filterGroup->addFilter(new GPUImageSharpenFilter(0.2f));
    m_GPUImageNormalBlendFilter = new GPUImageNormalBlendFilter();
    filterGroup->addFilter(m_GPUImageNormalBlendFilter);
    filterGroup->addFilter(m_GPUImageTextRender);
    m_GPUImageRenderer = new GPUImageRenderer(filterGroup);
#endif
}

VideoGLRender::~VideoGLRender() {
    RenderImageUtil::freeRenderImage(&m_RenderImage);
    if(m_GPUImageRenderer != nullptr) {
        delete m_GPUImageRenderer;
        m_GPUImageRenderer = nullptr;
    }
}

void VideoGLRender::Init(int width, int height, int *dstSize) {
    LogI("VideoGLRender::InitRender video[w, h]=[%d, %d]", width, height);
    if(dstSize != nullptr) {
        dstSize[0] = width;
        dstSize[1] = height;
    }
    m_FrameIndex = 0;
    m_Width = width;
    m_Height = height;
}

void VideoGLRender::RenderVideoFrame(RenderImage *pImage) {
    if(pImage == nullptr || pImage->planes[0] == nullptr)
        return;
    std::unique_lock<std::mutex> lock(m_Mutex);

    m_FrameNums++;

    char info[256];
    sprintf(info, "Frame: (%d, %d) idd: %d ", pImage->width, pImage->height, m_FrameNums);
    std::string tmpStr = info;

#ifdef __USE_PIXEL_BUFFER__
    if (m_RenderImage.width != pImage->width || m_RenderImage.height != pImage->height) {
        if (m_RenderImage.planes[0] != nullptr) {
            RenderImageUtil::freeRenderImage(&m_RenderImage);
        }
        memset(&m_RenderImage, 0, sizeof(m_RenderImage));
        m_RenderImage.format = IMAGE_FORMAT_RGBA;
        m_RenderImage.width = pImage->width;
        m_RenderImage.height = pImage->height;
        RenderImageUtil::allocRenderImage(&m_RenderImage);
    }

    RenderImage *filterdImage = nullptr;
    std::thread _thread([this, tmpStr, &pImage, &filterdImage](){
        PixelBuffer pixelBuffer(pImage->width, pImage->height);
        GPUImageFilterGroup *filterGroup = new GPUImageFilterGroup();
        GPUImageTextFilter *textFilter = new GPUImageTextFilter();
        textFilter->setMString(tmpStr);
        filterGroup->addFilter(new GPUImageRGBFilter(1.0f, 0.9f, 1.0f));
        filterGroup->addFilter(new GPUImageRGBFilter(0.9f, 1.0f, 1.0f));
        filterGroup->addFilter(new GPUImageGaussianBlurFilter(0.2f));
        filterGroup->addFilter(new GPUImageSharpenFilter(0.2f));

#if 1
        GPUImageNormalBlendFilter *normalBlendFilter = new GPUImageNormalBlendFilter();
        filterGroup->addFilter(normalBlendFilter);
//        filterGroup->addFilter(textFilter);

        cv::Mat inputImage = cv::imread("/sdcard/Download/baidu.png", -1);
        if (m_RenderImageSmall.width == 0 || m_RenderImageSmall.height == 0) {
            memset(&m_RenderImageSmall, 0, sizeof(m_RenderImageSmall));
            m_RenderImageSmall.format = IMAGE_FORMAT_RGBA;
            m_RenderImageSmall.width = inputImage.cols;
            m_RenderImageSmall.height = inputImage.rows;
            RenderImageUtil::allocRenderImage(&m_RenderImageSmall);
        }
        memcpy(m_RenderImageSmall.planes[0], inputImage.data,
               inputImage.cols * inputImage.rows * 4);

        normalBlendFilter->UpdateMVPMatrix( 0, 0, 0, 0, 0.5, 0.5);
        normalBlendFilter->setRenderImage(&m_RenderImageSmall);
#endif
        GPUImageRenderer *renderer = new GPUImageRenderer(filterGroup);
        pixelBuffer.setRenderer(renderer);
        filterdImage = pixelBuffer.getRenderImageWithFilterApplied(pImage);
    });
    _thread.join();

    RenderImageUtil::copyRenderImage(filterdImage, &m_RenderImage);
    RenderImageUtil::freeRenderImage(filterdImage);
    free(filterdImage);

#else
    if (m_RenderImage.width != pImage->width || m_RenderImage.height != pImage->height) {
        if (m_RenderImage.planes[0] != nullptr) {
            RenderImageUtil::freeRenderImage(&m_RenderImage);
        }
        memset(&m_RenderImage, 0, sizeof(m_RenderImage));
        m_RenderImage.format = pImage->format;
        m_RenderImage.width = pImage->width;
        m_RenderImage.height = pImage->height;
        RenderImageUtil::allocRenderImage(&m_RenderImage);
    }
    RenderImageUtil::copyRenderImage(pImage, &m_RenderImage);
    if (m_RenderImageSmall.width == 0 || m_RenderImageSmall.height == 0) {
#define __USE_OPENCV_LOAD_IMAGE__
#ifdef __USE_OPENCV_LOAD_IMAGE__
        cv::Mat inputImage = cv::imread("/sdcard/Download/baidu.png", -1);
        memset(&m_RenderImageSmall, 0, sizeof(m_RenderImageSmall));
        m_RenderImageSmall.format = IMAGE_FORMAT_RGBA;
        m_RenderImageSmall.width = inputImage.cols;
        m_RenderImageSmall.height = inputImage.rows;
        RenderImageUtil::allocRenderImage(&m_RenderImageSmall);
        memcpy(m_RenderImageSmall.planes[0], inputImage.data, inputImage.cols * inputImage.rows * 4);
#else
        memset(&m_RenderImageSmall, 0, sizeof(m_RenderImageSmall));
        m_RenderImageSmall.format = IMAGE_FORMAT_RGBA;
        m_RenderImageSmall.width = pImage->width / 4;
        m_RenderImageSmall.height = pImage->height / 4;
        RenderImageUtil::allocRenderImage(&m_RenderImageSmall);
        for(int i = 0; i < m_RenderImageSmall.height; i++) {
            for (int j = 0; j < m_RenderImageSmall.width; j++) {
                if(j < m_RenderImageSmall.width / 4 * 1) {
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4] = 255;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 1] = 0;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 2] = 0;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 3] = 127;
                } else if(j < m_RenderImageSmall.width / 4 * 2) {
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4] = 255;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 1] = 255;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 2] = 0;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 3] = 127;
                } else if(j < m_RenderImageSmall.width / 4 * 3) {
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4] = 0;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 1] = 255;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 2] = 0;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 3] = 127;
                } else if(j < m_RenderImageSmall.width) {
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4] = 0;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 1] = 255;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 2] = 255;
                    m_RenderImageSmall.planes[0][i * m_RenderImageSmall.width * 4 + j * 4 + 3] = 127;
                }
            }
        }
#endif
    }
    float scaleX = m_RenderImageSmall.width * 1.0f / m_RenderImage.width;
    float scaleY = m_RenderImageSmall.height * 1.0f / m_RenderImage.width;
    m_XAngle += 2;

    m_GPUImageNormalBlendFilter->UpdateMVPMatrix( -0.8, -0.7, 0, m_XAngle, scaleX, scaleX);
    m_GPUImageNormalBlendFilter->setRenderImage(&m_RenderImageSmall);
    m_GPUImageTextRender->setMString(tmpStr);
#endif
//    RenderImageUtil::dumpRenderImage(&m_RenderImage, "/sdcard/Download", "IMG");
    m_GPUImageRenderer->setRenderImage(&m_RenderImage);
}

void VideoGLRender::UnInit() {
    if(m_GLSurfaceView != nullptr) {
        zyb::jni::GetEnv()->DeleteGlobalRef(m_GLSurfaceView);
        m_GLSurfaceView = nullptr;
    }
}

void VideoGLRender::UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY)
{
    m_GPUImageRenderer->UpdateMVPMatrix(angleX, angleY, scaleX, scaleY);
}

void VideoGLRender::onVideoFrame(int playerID, bool first, int width, int height, int stride, int64_t ts, int len, void* data) {
    RenderImage image;
    image.format = IMAGE_FORMAT_I420;
    image.width = width;
    image.height = height;
    image.planes[0] = (uint8_t *)data;
    switch (image.format)
    {
        case IMAGE_FORMAT_RGBA:
            image.linesize[0] = stride * 4;
            break;
        case IMAGE_FORMAT_NV12:
        case IMAGE_FORMAT_NV21:
            image.planes[1] = image.planes[0] + stride * height;
            image.linesize[0] = width;
            image.linesize[1] = width;
            break;
        case IMAGE_FORMAT_I420:
            image.planes[1] = image.planes[0] + stride * height;
            image.planes[2] = image.planes[1] + stride * height / 4;
            image.linesize[0] = width;
            image.linesize[1] = width / 2;
            image.linesize[2] = width / 2;
            break;
        default:
            break;
    }
    RenderVideoFrame(&image);
    zyb::jni::GetEnv()->CallVoidMethod(m_GLSurfaceView, m_CallbackId);
}

void VideoGLRender::OnSurfaceCreated() {
    LogI("VideoGLRender::OnSurfaceCreated");

    m_GPUImageRenderer->onSurfaceCreated();
}

void VideoGLRender::OnSurfaceChanged(int width, int height) {
    LogI("VideoGLRender::OnSurfaceChanged [w, h]=[%d, %d]", width, height);
    m_ScreenSize.x = width;
    m_ScreenSize.y = height;

    m_InitDone = true;

    glViewport(0, 0, width, height);
    m_GPUImageRenderer->onSurfaceChanged(width, height);
}

void VideoGLRender::OnDrawFrame() {
    glClear(GL_COLOR_BUFFER_BIT);
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_GPUImageRenderer->onDrawFrame();
    lock.unlock();
}