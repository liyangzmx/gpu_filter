//
// Created by liyang on 21-7-5.
//

#ifndef ANDROID_PRJ_GPUIMAGETWOINPUTFILTER_H
#define ANDROID_PRJ_GPUIMAGETWOINPUTFILTER_H

#include <detail/type_mat.hpp>
#include <detail/type_mat4x4.hpp>
#include <gtc/matrix_transform.hpp>
#include "Rotation.h"
#include "GPUImageFilter.h"
#include "TextureRotationUtil.h"
#include "RenderImage.h"

class GPUImageTwoInputFilter : public GPUImageFilter {
public:
    static const char VERTEX_SHADER[];
    static const char VERTEX_SHADER_STR[];
    static const char FRAGMENT_SHADER_STR[];
    GPUImageTwoInputFilter(const char *fragmentShader);
    GPUImageTwoInputFilter(const char *vertexShader, const char *fragmentShader);
    ~GPUImageTwoInputFilter();
    void setRotation(Rotation rotation, bool filpHorizontal, bool filpVertical);
    void genTextures();
    void setRenderImage(RenderImage *image);
    void renderTexture(const float *cubeBuffer, const float *textureBuffer);
    void genFBTextures(RenderImage *image);

    virtual void onInit();
    virtual void onDrawArraysPre();
    virtual void onOutputSizeChanged(int width, int height);
    virtual void onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer);

private:
#define TEXTURE_NUM 3
#define MATH_PI 3.1415926535897932384626433832802
    float texture1CoordinatesBuffer[8] = { 0.0f };
    float texture2CoordinatesBuffer[8] = { 0.0f };
    GLuint filterSecondTextureCoordinateAttribute;
    GLuint filterInputTextureUniform2;
//    GLuint filterSourceTexture2 = 0xFFFFFFFF;
    GLuint m_ProgramObj;
    GLuint m_AttribPositionObj;
    GLuint m_AttribTextureCoordinateObj;
    GLuint glTextureId = 0xFFFFFFFF;
    GLuint glFrameBufferId;
    bool m_ImageLoaded = false;
    glm::mat4 m_MVPMatrix;
    GLuint m_TextureIds[TEXTURE_NUM];
    GLuint m_VaoId = -1;
    GLuint m_VboIds[TEXTURE_NUM];

    int imageWidth = 0;
    int imageHeight = 0;

    int textureWidth = 0;
    int textureHeight = 0;

    int m_RenderImageFormat = IMAGE_FORMAT_RGBA;

};


#endif //ANDROID_PRJ_GPUIMAGETWOINPUTFILTER_H
