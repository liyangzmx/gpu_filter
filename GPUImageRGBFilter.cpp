//
// Created by liyang on 21-6-25.
//

#include "GPUImageRGBFilter.h"

//const char *GPUImageRGBFilter::RGB_FRAGMENT_SHADER = "#version 300 es\n"
//                                                     "in vec2 textureCoordinate;\n"
//                                                     "\n"
//                                                     "uniform sampler2D inputImageTexture;\n"
//                                                     "uniform highp float red;\n"
//                                                     "uniform highp float green;\n"
//                                                     "uniform highp float blue;\n"
//                                                     "layout(location = 0) out vec4 gl_FragColor;\n"
//                                                     "void main()\n"
//                                                     "{\n"
//                                                     "    highp vec4 textureColor = texture(inputImageTexture, textureCoordinate);\n"
//                                                     "    \n"
//                                                     "    gl_FragColor = vec4(textureColor.r * red, textureColor.g * green, textureColor.b * blue, 1.0);\n"
//                                                     "}\n";

const char *GPUImageRGBFilter::RGB_FRAGMENT_SHADER = ""
                                                     "  varying highp vec2 textureCoordinate;\n"
                                                     "  \n"
                                                     "  uniform sampler2D inputImageTexture;\n"
                                                     "  uniform highp float red;\n"
                                                     "  uniform highp float green;\n"
                                                     "  uniform highp float blue;\n"
                                                     "  \n"
                                                     "  void main()\n"
                                                     "  {\n"
                                                     "      highp vec4 textureColor = texture2D(inputImageTexture, textureCoordinate);\n"
                                                     "      \n"
                                                     "      gl_FragColor = vec4(textureColor.r * red, textureColor.g * green, textureColor.b * blue, 1.0);\n"
                                                     "  }\n";

GPUImageRGBFilter::GPUImageRGBFilter(float red, float green, float blue) :
        GPUImageFilter(NO_FILTER_VERTEX_SHADER, RGB_FRAGMENT_SHADER),
        red(red), green(green), blue(blue)
{
    redLocation = 0;
    greenLocation = 0;
    blueLocation = 0;
}

GPUImageRGBFilter::~GPUImageRGBFilter() {

}

void GPUImageRGBFilter::setRed(float &_red) {
    red = _red;
    setFloat(redLocation, red);
}

void GPUImageRGBFilter::setGreen(float &_green) {
    green = _green;
    setFloat(greenLocation, green);
}

void GPUImageRGBFilter::setBlue(float &_blue) {
    blue = _blue;
    setFloat(blueLocation, blue);
}

void GPUImageRGBFilter::onInit() {
    GPUImageFilter::onInit();
    redLocation = glGetUniformLocation(getProgram(), "red");
    greenLocation = glGetUniformLocation(getProgram(), "green");
    blueLocation = glGetUniformLocation(getProgram(), "blue");
}

void GPUImageRGBFilter::onInitialized() {
    GPUImageFilter::onInitialized();
    setRed(red);
    setGreen(green);
    setBlue(blue);
}
