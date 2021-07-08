//
// Created by liyang on 21-6-25.
//

#include "TextureRotationUtil.h"
#include "GPUImageFilter.h"

const char *GPUImageFilter::NO_FILTER_VERTEX_SHADER = ""
                                                      "attribute vec4 position;\n"
                                                      "attribute vec4 inputTextureCoordinate;\n"
                                                      " \n"
                                                      "varying vec2 textureCoordinate;\n"
                                                      " \n"
                                                      "void main()\n"
                                                      "{\n"
                                                      "    gl_Position = position;\n"
                                                      "    textureCoordinate = inputTextureCoordinate.xy;\n"
                                                      "}";

const char *GPUImageFilter::NO_FILTER_FRAGMENT_SHADER = ""
                                                        "varying highp vec2 textureCoordinate;\n"
                                                        " \n"
                                                        "uniform sampler2D inputImageTexture;\n"
                                                        " \n"
                                                        "void main()\n"
                                                        "{\n"
                                                        "     gl_FragColor = texture2D(inputImageTexture, textureCoordinate);\n"
                                                        "}";

GPUImageFilter::GPUImageFilter(const char *vertexShader, const char *fragmentShader, bool isGroupFilter) {
    m_VertexShader = vertexShader;
    m_FragmentShader = fragmentShader;
    m_IsInitialized = false;
    m_IsGroupFilter = isGroupFilter;
}

GPUImageFilter::~GPUImageFilter(){
    m_IsInitialized = false;
    if(m_ProgramId != GL_NONE) {
        glDeleteProgram(m_ProgramId);
    }
}

void GPUImageFilter::onInit() {
    m_ProgramId = GLUtils::CreateProgram(m_VertexShader, m_FragmentShader);
    m_AttribPosition = glGetAttribLocation(m_ProgramId, "position");
    m_UniformTexture = glGetUniformLocation(m_ProgramId, "inputImageTexture");
    m_AttribTextureCoordinate = glGetAttribLocation(m_ProgramId, "inputTextureCoordinate");
    m_IsInitialized = true;
}

void GPUImageFilter::onInitialized() {}

void GPUImageFilter::onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer) {
    glUseProgram(m_ProgramId);
    runPendingOnDrawTasks();

    if (!m_IsInitialized) {
        return;
    }

    glEnableVertexAttribArray(m_AttribPosition);
    glVertexAttribPointer(m_AttribPosition, 2, GL_FLOAT, false, 8, cubeBuffer);
    glEnableVertexAttribArray(m_AttribTextureCoordinate);
    glVertexAttribPointer(m_AttribTextureCoordinate, 2, GL_FLOAT, false, 8, textureBuffer);
    if (textureId != -1) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glUniform1i(m_UniformTexture, 0);
    }
    onDrawArraysPre();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(m_AttribPosition);
    glDisableVertexAttribArray(m_AttribTextureCoordinate);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GPUImageFilter::onDrawArraysPre() {}

void GPUImageFilter::ifNeedInit() {
    if (!m_IsInitialized) init();
}

void GPUImageFilter::onOutputSizeChanged(int width, int height) {
    m_Width = width;
    m_Height = height;
}

bool GPUImageFilter::isInitialized() const {
    return m_IsInitialized;
}

int GPUImageFilter::getOutputWidth() {
    return m_Width;
}

int GPUImageFilter::getOutputHeight() {
    return m_Height;
}

GLuint GPUImageFilter::getProgram() {
    return m_ProgramId;
}

GLuint GPUImageFilter::getAttribPosition() {
    return m_AttribPosition;
}

GLuint GPUImageFilter::getAttribTextureCoordinate() {
    return m_AttribTextureCoordinate;
}

GLuint GPUImageFilter::UniformTexture() {
    return m_UniformTexture;
}

void GPUImageFilter::setInteger(const int location, int intValue) {
    runOnDraw([this, location, intValue]() {
        ifNeedInit();
        glUniform1i(location, intValue);
    });
}

void GPUImageFilter::setFloat(const int location, float floatValue) {
    runOnDraw([this, location, floatValue]() {
        ifNeedInit();
        glUniform1f(location, floatValue);
    });
}

void GPUImageFilter::setFloatVec2(const int location, const float *arrayValue) {
    runOnDraw([this, location, arrayValue]() {
        ifNeedInit();
        glUniform2fv(location, 1, arrayValue);
    });
}

void GPUImageFilter::setFloatVec3(const int location, const float *arrayValue) {
    runOnDraw([this, location, arrayValue]() {
        ifNeedInit();
        glUniform3fv(location, 1, arrayValue);
    });
}

void GPUImageFilter::setFloatVec4(const int location, const float *arrayValue) {
    runOnDraw([this, location, arrayValue]() {
        ifNeedInit();
        glUniform4fv(location, 1, arrayValue);
    });
}

void GPUImageFilter::setFloatArray(const int location, const float *arrayValue, int length) {
    runOnDraw([this, location, length, arrayValue]() {
        ifNeedInit();
        glUniform1fv(location, length, arrayValue);
    });
}

void GPUImageFilter::setFloatArray(const int location, const glm::vec2 value) {
    runOnDraw([this, location, value]() {
        ifNeedInit();
        glUniform1fv(location, value.length(), &value[0]);
    });
}

void GPUImageFilter::setUniformMatrix3f(const int location, const float *matrix) {
    runOnDraw([this, location, matrix]() {
        ifNeedInit();
        glUniformMatrix3fv(location, 1, GL_FALSE, matrix);
    });
}

void GPUImageFilter::setUniformMatrix4f(const int location, const float *matrix) {
    runOnDraw([this, location, matrix]() {
        ifNeedInit();
        glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
    });
}

bool GPUImageFilter::isMIsGroupFilter() const {
    return m_IsGroupFilter;
}