//
// Created by liyang on 21-7-5.
//

#include "GPUImageSharpenFilter.h"

const char *GPUImageSharpenFilter::SHARPEN_VERTEX_SHADER = ""
                                                           "attribute vec4 position;\n"
                                                           "attribute vec4 inputTextureCoordinate;\n"
                                                           "\n"
                                                           "uniform float imageWidthFactor; \n"
                                                           "uniform float imageHeightFactor; \n"
                                                           "uniform float sharpness;\n"
                                                           "\n"
                                                           "varying vec2 textureCoordinate;\n"
                                                           "varying vec2 leftTextureCoordinate;\n"
                                                           "varying vec2 rightTextureCoordinate; \n"
                                                           "varying vec2 topTextureCoordinate;\n"
                                                           "varying vec2 bottomTextureCoordinate;\n"
                                                           "\n"
                                                           "varying float centerMultiplier;\n"
                                                           "varying float edgeMultiplier;\n"
                                                           "\n"
                                                           "void main()\n"
                                                           "{\n"
                                                           "    gl_Position = position;\n"
                                                           "    \n"
                                                           "    mediump vec2 widthStep = vec2(imageWidthFactor, 0.0);\n"
                                                           "    mediump vec2 heightStep = vec2(0.0, imageHeightFactor);\n"
                                                           "    \n"
                                                           "    textureCoordinate = inputTextureCoordinate.xy;\n"
                                                           "    leftTextureCoordinate = inputTextureCoordinate.xy - widthStep;\n"
                                                           "    rightTextureCoordinate = inputTextureCoordinate.xy + widthStep;\n"
                                                           "    topTextureCoordinate = inputTextureCoordinate.xy + heightStep;     \n"
                                                           "    bottomTextureCoordinate = inputTextureCoordinate.xy - heightStep;\n"
                                                           "    \n"
                                                           "    centerMultiplier = 1.0 + 4.0 * sharpness;\n"
                                                           "    edgeMultiplier = sharpness;\n"
                                                           "}";

const char *GPUImageSharpenFilter::SHARPEN_FRAGMENT_SHADER = ""
                                                             "precision highp float;\n"
                                                             "\n"
                                                             "varying highp vec2 textureCoordinate;\n"
                                                             "varying highp vec2 leftTextureCoordinate;\n"
                                                             "varying highp vec2 rightTextureCoordinate; \n"
                                                             "varying highp vec2 topTextureCoordinate;\n"
                                                             "varying highp vec2 bottomTextureCoordinate;\n"
                                                             "\n"
                                                             "varying highp float centerMultiplier;\n"
                                                             "varying highp float edgeMultiplier;\n"
                                                             "\n"
                                                             "uniform sampler2D inputImageTexture;\n"
                                                             "\n"
                                                             "void main()\n"
                                                             "{\n"
                                                             "    mediump vec3 textureColor = texture2D(inputImageTexture, textureCoordinate).rgb;\n"
                                                             "    mediump vec3 leftTextureColor = texture2D(inputImageTexture, leftTextureCoordinate).rgb;\n"
                                                             "    mediump vec3 rightTextureColor = texture2D(inputImageTexture, rightTextureCoordinate).rgb;\n"
                                                             "    mediump vec3 topTextureColor = texture2D(inputImageTexture, topTextureCoordinate).rgb;\n"
                                                             "    mediump vec3 bottomTextureColor = texture2D(inputImageTexture, bottomTextureCoordinate).rgb;\n"
                                                             "\n"
                                                             "    gl_FragColor = vec4((textureColor * centerMultiplier - (leftTextureColor * edgeMultiplier + rightTextureColor * edgeMultiplier + topTextureColor * edgeMultiplier + bottomTextureColor * edgeMultiplier)), texture2D(inputImageTexture, bottomTextureCoordinate).w);\n"
                                                             "}";

void GPUImageSharpenFilter::onInit() {
    GPUImageFilter::onInit();

    sharpnessLocation = glGetUniformLocation(getProgram(), "sharpness");
    imageWidthFactorLocation = glGetUniformLocation(getProgram(), "imageWidthFactor");
    imageHeightFactorLocation = glGetUniformLocation(getProgram(), "imageHeightFactor");
}

void GPUImageSharpenFilter::onInitialized() {
    GPUImageFilter::onInitialized();
    setSharpness(sharpness);
}

void GPUImageSharpenFilter::setSharpness(const GLfloat _sharpness) {
    sharpness = _sharpness;
    setFloat(sharpnessLocation, sharpness);
}

void GPUImageSharpenFilter::onOutputSizeChanged(int width, int height) {
    GPUImageFilter::onOutputSizeChanged(width, height);

    setFloat(imageWidthFactorLocation, 1.0f / width);
    setFloat(imageHeightFactorLocation, 1.0f / height);
}
