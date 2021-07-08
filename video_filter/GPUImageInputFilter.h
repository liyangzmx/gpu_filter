//
// Created by liyang on 21-7-5.
//

#ifndef ANDROID_PRJ_GPUIMAGEINPUTFILTER_H
#define ANDROID_PRJ_GPUIMAGEINPUTFILTER_H

#include <detail/type_mat.hpp>
#include <detail/type_mat4x4.hpp>
#include <gtc/matrix_transform.hpp>
#include "Rotation.h"
#include "RenderImage.h"
#include "GPUImageFilter.h"
#include "ScaleType.h"

class GPUImageInputFilter : public GPUImageFilter {
public:
    static const char VERTEX_SHADER_STR[];
    static const char FRAGMENT_SHADER_STR[];

    virtual ~GPUImageInputFilter();

    GPUImageInputFilter() : GPUImageFilter(VERTEX_SHADER_STR, FRAGMENT_SHADER_STR){}
    void setRenderImage(RenderImage *image);
    void deleteImage();

    virtual void onInit();

    virtual void onInitialized();

    virtual void onDrawArraysPre();
    virtual void onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer);

private:
#define TEXTURE_NUM 3
    GLuint m_TextureIds[TEXTURE_NUM];
    GLuint m_ProgramObj = GL_NONE;
    GLuint m_VaoId = -1;
    GLuint m_VboIds[TEXTURE_NUM];
    glm::mat4 m_MVPMatrix;

    int m_RenderImageFormat = IMAGE_FORMAT_RGBA;
    Rotation rotation;
    bool flipHorizontal = false;
    bool flipVertical = false;
    ScaleType scaleType = CENTER_CROP;
};


#endif //ANDROID_PRJ_GPUIMAGEINPUTFILTER_H
