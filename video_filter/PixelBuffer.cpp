//
// Created by liyang on 21-6-28.
//

#include "PixelBuffer.h"

PixelBuffer::PixelBuffer(int width, int height) : m_Width(width), m_Height(height){
    int version[2] = {0};
    int attribList[] = {
        EGL_WIDTH, m_Width,
        EGL_HEIGHT, m_Height,
        EGL_NONE
    };

    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    bool ret = eglInitialize(eglDisplay, &version[0], &version[1]);
    if(ret == false) {
        std::cout << "eglInitialize() ret false" << std::endl;
        return ;
    }
    eglConfig = chooseConfig(); // Choosing a config is a little more
    // complicated

    // eglContext = eglCreateContext(eglDisplay, eglConfig,
    // EGL_NO_CONTEXT, null);
//    int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
    int attrib_list[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, attrib_list);
    eglSurface = eglCreatePbufferSurface(eglDisplay, eglConfig, attribList);
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
    m_ThreadId = std::this_thread::get_id();
//    mThreadOwner = Thread.currentThread().getName();
}

PixelBuffer::~PixelBuffer() {
    if(m_Renderer != nullptr) {
        delete m_Renderer;
        m_Renderer = nullptr;
    }
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE,
                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(eglDisplay, eglSurface);
    eglDestroyContext(eglDisplay, eglContext);
    eglTerminate(eglDisplay);
}

void PixelBuffer::setRenderer(GPUImageRenderer *renderer) {
    m_Renderer = renderer;
    std::thread::id currentThreadId = std::this_thread::get_id();
    if(m_ThreadId != currentThreadId) {
        std::cout << "JMS: PixelBuffer::setRenderer(): This thread does not own the OpenGL context." << std::endl;
        return;
    }
    m_Renderer->onSurfaceCreated();
    m_Renderer->onSurfaceChanged(m_Width, m_Height);
}

RenderImage *PixelBuffer::getRenderImage() {
    if(m_Renderer == nullptr) {
        std::cout << "JMS: getBitmap: Renderer was not set." << std::endl;
        return nullptr;
    }
    if(m_ThreadId != std::this_thread::get_id()) {
        std::cout << "JMS: PixelBuffer::getBitmap(): This thread does not own the OpenGL context." << std::endl;
        return nullptr;
    }

    // Call the renderer draw routine (it seems that some filters do not
    // work if this is only called once)
    m_Renderer->onDrawFrame();

    RenderImage *image = new RenderImage();
    image->format = IMAGE_FORMAT_RGBA;
    image->width = m_Width;
    image->height = m_Height;

    RenderImageUtil::allocRenderImage(image);
    glReadPixels(0, 0, image->width, image->height, GL_RGBA, GL_UNSIGNED_BYTE, image->planes[0]);

    m_Renderer->deleteImage();
    m_Renderer->onDrawFrame();

    return image;
}

int PixelBuffer::getRenderImageRGB(uint8_t *rgbData) {
    if(m_Renderer == nullptr) {
        std::cout << "JMS: getBitmap: Renderer was not set." << std::endl;
        return -1;
    }
    if(m_ThreadId != std::this_thread::get_id()) {
        std::cout << "JMS: PixelBuffer::getBitmap(): This thread does not own the OpenGL context." << std::endl;
        return -1;
    }

    // Call the renderer draw routine (it seems that some filters do not
    // work if this is only called once)
    m_Renderer->onDrawFrame();
    m_Renderer->deleteImage();
    m_Renderer->onDrawFrame();

    RenderImage *image = new RenderImage();
    image->format = IMAGE_FORMAT_RGBA;
    image->width = m_Width;
    image->height = m_Height;
    image->linesize[0] = m_Width;

    RenderImageUtil::allocRenderImage(image);

    glReadPixels(0, 0, image->width, image->height, GL_RGBA, GL_UNSIGNED_BYTE, &(image->planes[0]));
    return 0;
}


EGLConfig PixelBuffer::chooseConfig() {
    int attribList[] = {
            EGL_DEPTH_SIZE, 0,
            EGL_STENCIL_SIZE, 0,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, 4,
            EGL_NONE
    };
    int numConfig = 0;
    eglChooseConfig(eglDisplay, attribList, nullptr, 0, &numConfig);
    eglConfigs = (EGLConfig *)malloc(sizeof(EGLConfig) * numConfig);
    eglChooseConfig(eglDisplay, attribList, eglConfigs, numConfig, &numConfig);

    if (LIST_CONFIGS) {
        listConfig();
    }

    return eglConfigs[0];
}

void PixelBuffer::listConfig() {

}

int PixelBuffer::getConfigAttrib(EGLConfig config, int attrib) {
    return 0;
}

RenderImage *PixelBuffer::getRenderImageWithFilterApplied(RenderImage *image) {
    if(m_Renderer != nullptr) {
        m_Renderer->setRenderImage(image);
    }
    return getRenderImage();
}