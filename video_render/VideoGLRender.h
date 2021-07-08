#ifndef __ZYB_MASTER_GLRENDER_H__
#define __ZYB_MASTER_GLRENDER_H__
#include <thread>
#include <jni.h>
#include "../../../../../../src/video_render/IZybVideoRender.h"
#include <GLES3/gl3.h>
#include <detail/type_mat.hpp>
#include <detail/type_mat4x4.hpp>
#include <vec2.hpp>
#include "BaseGLRender.h"
#include "../../../../../../src/include/Globals.h"
#include "../jni_helpers.h"
#include "../video_filter/RenderImage.h"
#include "../video_filter/GPUImageFilter.h"
#include "../video_filter/GPUImageFilterGroup.h"
#include "../video_filter/GPUImageRenderer.h"
#include "../video_filter/GPUImageRGBFilter.h"
#include "../video_filter/GPUImageTextFilter.h"
#include "../video_filter/GPUImageTwoPassFilter.h"
#include "../video_filter/GPUImageTwoPassTextureSamplingFilter.h"
#include "../video_filter/GPUImageTwoInputFilter.h"
#include "../video_filter/GPUImageNormalBlendFilter.h"

using namespace glm;

#define MATH_PI 3.1415926535897932384626433832802
#define TEXTURE_NUM 3

class VideoGLRender: public zyb::VideoRenderEvent, public BaseGLRender{
public:
    virtual void Init(int width, int height, int *dstSize);
    virtual void RenderVideoFrame(RenderImage *pImage);
    virtual void UnInit();

    virtual void OnSurfaceCreated();
    virtual void OnSurfaceChanged(int w, int h);
    virtual void OnDrawFrame();
    virtual void UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY);
    virtual void SetTouchLoc(float touchX, float touchY) {
        m_TouchXY.x = touchX / m_ScreenSize.x;
        m_TouchXY.y = touchY / m_ScreenSize.y;
    }
    virtual void onVideoFrame(int playerID, bool first, int width, int height, int stride, int64_t ts, int len, void* data);

    VideoGLRender();
    virtual ~VideoGLRender();

    void SetCallback(jobject glSurfaceView) {
        m_GLSurfaceView = zyb::jni::GetEnv()->NewGlobalRef(glSurfaceView);

        jclass clz = zyb::jni::GetEnv()->GetObjectClass(m_GLSurfaceView);
        m_CallbackId = zyb::jni::GetEnv()->GetMethodID(clz, "requestRender", "()V");
    }

private:

    jobject m_GLSurfaceView;
    jmethodID m_CallbackId;

    static std::mutex m_Mutex;
    static VideoGLRender* s_Instance;
    GLuint m_ProgramObj = GL_NONE;
    GLuint m_TextureIds[TEXTURE_NUM];
    GLuint m_VaoId;
    GLuint m_VboIds[3];
    GLuint m_FrameBuffer;
    GLuint m_FrameBufferTexture;
    RenderImage m_RenderImageRGB;
    RenderImage m_RenderImage;
    RenderImage m_RenderImageSmall;
    glm::mat4 m_MVPMatrix;
    int m_Width;
    int m_Height;
    bool m_InitDone;

    GPUImageRenderer *m_GPUImageRenderer;
    GPUImageTextFilter *m_GPUImageTextRender;
    GPUImageNormalBlendFilter *m_GPUImageNormalBlendFilter;
    int m_FrameNums = 0;

    int m_FrameIndex;
    vec2 m_TouchXY;
    vec2 m_ScreenSize;
};


#endif //__ZYB_MASTER_GLRENDER_H__
