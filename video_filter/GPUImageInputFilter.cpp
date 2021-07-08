//
// Created by liyang on 21-7-5.
//

#include "GPUImageInputFilter.h"

const char GPUImageInputFilter::VERTEX_SHADER_STR[] =
        "#version 300 es\n"
        "layout(location = 0) in vec4 a_position;\n"
        "layout(location = 1) in vec2 a_texCoord;\n"
        "out vec2 v_texCoord;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = a_position;\n"
        "    v_texCoord = a_texCoord;\n"
        "}";

const char GPUImageInputFilter::FRAGMENT_SHADER_STR[] =
        "#version 300 es\n"
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

void GPUImageInputFilter::setRenderImage(RenderImage *image) {
    m_RenderImageFormat = image->format;
    runOnDraw([this, image]() {
        switch (image->format) {
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

void GPUImageInputFilter::onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer) {
    glUseProgram(m_ProgramId);
    runPendingOnDrawTasks();

    if (!m_IsInitialized) {
        return;
    }

    glEnableVertexAttribArray(m_AttribPosition);
    glVertexAttribPointer(m_AttribPosition, 2, GL_FLOAT, false, 8, cubeBuffer);
    glEnableVertexAttribArray(m_AttribTextureCoordinate);
    glVertexAttribPointer(m_AttribTextureCoordinate, 2, GL_FLOAT, false, 8, textureBuffer);
    for (int i = 0; i < TEXTURE_NUM; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_TextureIds[i]);
        char samplerName[64] = {0};
        sprintf(samplerName, "s_texture%d", i);
        GLUtils::setInt(m_ProgramId, samplerName, i);
    }
    onDrawArraysPre();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(m_AttribPosition);
    glDisableVertexAttribArray(m_AttribTextureCoordinate);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GPUImageInputFilter::onDrawArraysPre() {
    GLUtils::setInt(m_ProgramId, "u_nImgType", m_RenderImageFormat);
}

void GPUImageInputFilter::onInitialized() {
    GPUImageFilter::onInitialized();

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

void GPUImageInputFilter::onInit() {
    m_ProgramId = GLUtils::CreateProgram(m_VertexShader, m_FragmentShader);
    m_AttribPosition = glGetAttribLocation(m_ProgramId, "a_position");
    m_AttribTextureCoordinate = glGetAttribLocation(m_ProgramId, "a_texCoord");
    m_IsInitialized = true;
}

GPUImageInputFilter::~GPUImageInputFilter() {
    glDeleteTextures(TEXTURE_NUM, m_TextureIds);
}
