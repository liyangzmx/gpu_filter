//
// Created by liyang on 21-7-5.
//

#include "GPUImageTwoInputFilter.h"

const char GPUImageTwoInputFilter::VERTEX_SHADER[] = "attribute vec4 position;\n"
                                                    "attribute vec4 inputTextureCoordinate;\n"
                                                    "attribute vec4 inputTextureCoordinate2;\n"
                                                    " \n"
                                                    "varying vec2 textureCoordinate;\n"
                                                    "varying vec2 textureCoordinate2;\n"
                                                    " \n"
                                                    "void main()\n"
                                                    "{\n"
                                                    "    gl_Position = position;\n"
                                                    "    textureCoordinate = inputTextureCoordinate.xy;\n"
                                                    "    textureCoordinate2 = inputTextureCoordinate2.xy;\n"
                                                    "}";

const char GPUImageTwoInputFilter::VERTEX_SHADER_STR[] = "#version 300 es\n"
                                                         "layout(location = 0) in vec4 a_position;\n"
                                                         "layout(location = 1) in vec2 a_texCoord;\n"
                                                         "out vec2 v_texCoord;\n"
                                                         "void main()\n"
                                                         "{\n"
                                                         "    gl_Position = a_position;\n"
                                                         "    v_texCoord = a_texCoord;\n"
                                                         "}";

const char GPUImageTwoInputFilter::FRAGMENT_SHADER_STR[] = "#version 300 es\n"
                                                           "precision highp float;\n"
                                                           "in vec2 v_texCoord;\n"
                                                           "layout(location = 0) out vec4 outColor;\n"
                                                           "uniform sampler2D s_texture0;\n"
                                                           "uniform sampler2D s_texture1;\n"
                                                           "uniform sampler2D s_texture2;\n"
                                                           "uniform int u_nImgType;// 1:RGBA, 2:NV21, 3:NV12, 4:I420\n"
                                                           "\n"
                                                           "void main()\n"
                                                           "{\n"
                                                           "\n"
                                                           "    if(u_nImgType == 1) //RGBA\n"
                                                           "    {\n"
                                                           "        outColor = texture(s_texture0, v_texCoord);\n"
                                                           "    }\n"
                                                           "    else if(u_nImgType == 2) //NV21\n"
                                                           "    {\n"
                                                           "        vec3 yuv;\n"
                                                           "        yuv.x = texture(s_texture0, v_texCoord).r;\n"
                                                           "        yuv.y = texture(s_texture1, v_texCoord).a - 0.5;\n"
                                                           "        yuv.z = texture(s_texture1, v_texCoord).r - 0.5;\n"
                                                           "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
                                                           "        0.0, \t-0.344, \t1.770,\n"
                                                           "        1.403,  -0.714,     0.0) * yuv;\n"
                                                           "        outColor = vec4(rgb, 1.0);\n"
                                                           "\n"
                                                           "    }\n"
                                                           "    else if(u_nImgType == 3) //NV12\n"
                                                           "    {\n"
                                                           "        vec3 yuv;\n"
                                                           "        yuv.x = texture(s_texture0, v_texCoord).r;\n"
                                                           "        yuv.y = texture(s_texture1, v_texCoord).r - 0.5;\n"
                                                           "        yuv.z = texture(s_texture1, v_texCoord).a - 0.5;\n"
                                                           "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
                                                           "        0.0, \t-0.344, \t1.770,\n"
                                                           "        1.403,  -0.714,     0.0) * yuv;\n"
                                                           "        outColor = vec4(rgb, 1.0);\n"
                                                           "    }\n"
                                                           "    else if(u_nImgType == 4) //I420\n"
                                                           "    {\n"
                                                           "        vec3 yuv;\n"
                                                           "        yuv.x = texture(s_texture0, v_texCoord).r;\n"
                                                           "        yuv.y = texture(s_texture1, v_texCoord).r - 0.5;\n"
                                                           "        yuv.z = texture(s_texture2, v_texCoord).r - 0.5;\n"
                                                           "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
                                                           "                              0.0, \t-0.344, \t1.770,\n"
                                                           "                              1.403,  -0.714,     0.0) * yuv;\n"
                                                           "        outColor = vec4(rgb, 1.0);\n"
                                                           "    }\n"
                                                           "    else\n"
                                                           "    {\n"
                                                           "        outColor = vec4(1.0);\n"
                                                           "    }\n"
                                                           "}";

GPUImageTwoInputFilter::GPUImageTwoInputFilter(const char *fragmentShader) : GPUImageTwoInputFilter(VERTEX_SHADER, fragmentShader) {}

GPUImageTwoInputFilter::GPUImageTwoInputFilter(const char *vertexShader,
                                               const char *fragmentShader) : GPUImageFilter(vertexShader, fragmentShader) {
    setRotation(NORMAL, false, false);
}

GPUImageTwoInputFilter::~GPUImageTwoInputFilter()  {
    if(m_ProgramObj != GL_NONE) {
        glDeleteProgram(m_ProgramObj);
        glDeleteTextures(1, &glTextureId);
        glTextureId = 0xFFFFFFFF;
        glDeleteTextures(TEXTURE_NUM, m_TextureIds);
    }
}


void GPUImageTwoInputFilter::setRotation(Rotation rotation, bool filpHorizontal, bool filpVertical) {
    TextureRotationUtil::getRotation( texture2CoordinatesBuffer, rotation, filpHorizontal, filpVertical);
}

void GPUImageTwoInputFilter::onDrawArraysPre() {
    glEnableVertexAttribArray(filterSecondTextureCoordinateAttribute);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, glTextureId);
    glUniform1i(filterInputTextureUniform2, 7);

    glVertexAttribPointer(filterSecondTextureCoordinateAttribute, 2, GL_FLOAT, false, 0, texture2CoordinatesBuffer);
}

void GPUImageTwoInputFilter::onInit() {
    m_ProgramId = GLUtils::CreateProgram(m_VertexShader, m_FragmentShader);
    m_AttribPosition = glGetAttribLocation(m_ProgramId, "position");
    m_UniformTexture = glGetUniformLocation(m_ProgramId, "inputImageTexture");
    m_AttribTextureCoordinate = glGetAttribLocation(m_ProgramId, "inputTextureCoordinate");

    filterSecondTextureCoordinateAttribute = glGetAttribLocation(getProgram(), "inputTextureCoordinate2");
    filterInputTextureUniform2 = glGetUniformLocation(getProgram(), "inputImageTexture2"); // This does assume a name of "inputImageTexture2" for second input texture in the fragment shader
    glEnableVertexAttribArray(filterSecondTextureCoordinateAttribute);

    genTextures();
    m_IsInitialized = true;
}

void GPUImageTwoInputFilter::genTextures() {
//    UpdateMVPMatrix(0, 0, 1.0f, 1.0f);
    m_ProgramObj = GLUtils::CreateProgram(VERTEX_SHADER_STR, FRAGMENT_SHADER_STR);
    if (!m_ProgramObj)
    {
        return;
    }

    m_AttribPositionObj = glGetAttribLocation(m_ProgramObj, "a_position");
    m_AttribTextureCoordinateObj = glGetAttribLocation(m_ProgramObj, "a_texCoord");

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(TEXTURE_NUM, m_TextureIds);
    for (int i = 0; i < TEXTURE_NUM ; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_TextureIds[i]);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, GL_NONE);
    }
}

void GPUImageTwoInputFilter::setRenderImage(RenderImage *image) {
    m_RenderImageFormat = image->format;
    if (imageWidth != image->width) {
        imageWidth = image->width;
        imageHeight = image->height;
    }
    runOnDraw([this, image]() {
        genFBTextures(image);

        switch(image->format) {
            case IMAGE_FORMAT_RGBA:
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_TextureIds[0]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width,
                             image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             image->planes[0]);
                glBindTexture(GL_TEXTURE_2D, GL_NONE);
                break;
            case IMAGE_FORMAT_NV12:
            case IMAGE_FORMAT_NV21:
                //upload Y plane data
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_TextureIds[0]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, image->width,
                             image->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                             image->planes[0]);
                glBindTexture(GL_TEXTURE_2D, GL_NONE);

                //update UV plane data
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m_TextureIds[1]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, image->width >> 1,
                             image->height >> 1, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                             image->planes[1]);
                glBindTexture(GL_TEXTURE_2D, GL_NONE);
                break;
            case IMAGE_FORMAT_I420:
                //upload Y plane data
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_TextureIds[0]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, image->width,
                             image->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                             image->planes[0]);
                glBindTexture(GL_TEXTURE_2D, GL_NONE);

                //update U plane data
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m_TextureIds[1]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, image->width >> 1,
                             image->height >> 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                             image->planes[1]);
                glBindTexture(GL_TEXTURE_2D, GL_NONE);

                //update V plane data
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, m_TextureIds[2]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, image->width >> 1,
                             image->height >> 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                             image->planes[2]);
                glBindTexture(GL_TEXTURE_2D, GL_NONE);
                break;
        }
    });
}

void GPUImageTwoInputFilter::renderTexture(const float *cubeBuffer, const float *textureBuffer) {
    glUseProgram (m_ProgramObj);
    glBindFramebuffer(GL_FRAMEBUFFER, glFrameBufferId);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); //禁用byte-alignment限制
//    glEnable(GL_BLEND);
//    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnableVertexAttribArray(m_AttribPositionObj);
    float temp[8] = { 0.0f };
    for(int i = 0; i < 8; i++) {
        temp[i] = cubeBuffer[i] / 3;
    }
    for(int i = 0; i < 8; i++) {
        temp[i] = temp[i] - 1.0 / 3 - 0.1;
    }
    glVertexAttribPointer(m_AttribPositionObj, 2, GL_FLOAT, false, 8, temp);
    glEnableVertexAttribArray(m_AttribTextureCoordinateObj);
    glVertexAttribPointer(m_AttribTextureCoordinateObj, 2, GL_FLOAT, false, 8, textureBuffer);

    for (int i = 0; i < TEXTURE_NUM; ++i) {
        glActiveTexture(GL_TEXTURE4 + i);
        glBindTexture(GL_TEXTURE_2D, m_TextureIds[i]);
        char samplerName[64] = {0};
        sprintf(samplerName, "s_texture%d", i);
        GLUtils::setInt(m_ProgramObj, samplerName, 4 + i);
    }
    GLUtils::setInt(m_ProgramObj, "u_nImgType", m_RenderImageFormat);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void
GPUImageTwoInputFilter::onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer) {
    GPUImageFilter::runPendingOnDrawTasks();
    if(!m_ImageLoaded || !m_IsInitialized) {
        return ;
    }
    renderTexture(cubeBuffer, textureBuffer);
    GPUImageFilter::onDraw(textureId, cubeBuffer, textureBuffer);
}

void GPUImageTwoInputFilter::genFBTextures(RenderImage *image)  {
    if (glTextureId == 0xFFFFFFFF) {
        glGenFramebuffers(1, &glFrameBufferId);
        glGenTextures(1, &glTextureId);

        glBindTexture(GL_TEXTURE_2D, glTextureId);
        glTexParameterf(GL_TEXTURE_2D,
                        GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D,
                        GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D,
                        GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D,
                        GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, glFrameBufferId);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, glTextureId, 0);

        glBindTexture(GL_TEXTURE_2D, GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
    }
    m_ImageLoaded = true;
}
