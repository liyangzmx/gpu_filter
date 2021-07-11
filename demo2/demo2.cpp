#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>

#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>

#include <EGL/egl.h>
#define GLFW_INCLUDE_ES3 1
#include <GLFW/glfw3.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#include <glm/gtc/matrix_transform.hpp>

#include "GLUtils.h"
#include "GLUtils.h"
#include "PixelBuffer.h"
#include "GPUImageFilter.h"
#include "GPUImageRGBFilter.h"
#include "GPUImageTextFilter.h"
#include "GPUImageRenderer.h"
#include "GPUImageFilterGroup.h"
#include "GPUImageGaussianBlurFilter.h"
#include "GPUImageSharpenFilter.h"
#include "GPUImageBilateralBlurFilter.h"
#include "GPUImageTwoInputFilter.h"
#include "GPUImageNormalBlendFilter.h"

int main(const int argc, const char *argv[]){
    RenderImage image, logo; int m_XAngle = 0, frameNums = 0;
    cv::Mat inputImage = cv::imread("../test.png", 1);
    
    memset(&image, 0, sizeof(image));
    image.format = IMAGE_FORMAT_RGBA;
    image.width = inputImage.cols;
    image.height = inputImage.rows;
    RenderImageUtil::allocRenderImage(&image);
    for(int i = 0; i < inputImage.rows; i++) {
        for(int j = 0; j < inputImage.cols; j++) {
            image.planes[0][i * inputImage.cols * 4 + j * 4] = inputImage.data[i * inputImage.cols * 3 + j * 3 + 2];
            image.planes[0][i * inputImage.cols * 4 + j * 4 + 1] = inputImage.data[i * inputImage.cols * 3 + j * 3 + 1];
            image.planes[0][i * inputImage.cols * 4 + j * 4 + 2] = inputImage.data[i * inputImage.cols * 3 + j * 3 + 0];
            image.planes[0][i * inputImage.cols * 4 + j * 4 + 3] = 255;
        }
    }

    cv::Mat smallImage = cv::imread("../baidu.png", -1);
    memset(&logo, 0, sizeof(logo));
    logo.format = IMAGE_FORMAT_RGBA;
    logo.width = smallImage.cols;
    logo.height = smallImage.rows;
    RenderImageUtil::allocRenderImage(&logo);
    memcpy(logo.planes[0], smallImage.data, smallImage.cols * smallImage.rows * 4);

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow *window = glfwCreateWindow(inputImage.cols / 2, inputImage.rows / 2, __FILE__, NULL, NULL);
    glfwMakeContextCurrent(window);

    float scaleX = logo.width * 1.0f / image.width;
    float scaleY = logo.height * 1.0f / image.width;

    GPUImageRenderer *renderer;
    GPUImageFilterGroup *filterGroup = new GPUImageFilterGroup();
    filterGroup->addFilter(new GPUImageRGBFilter(1.0f, 1.0f, 1.0f));
    filterGroup->addFilter(new GPUImageGaussianBlurFilter(1));
    GPUImageNormalBlendFilter *blendFliter = new GPUImageNormalBlendFilter();
    filterGroup->addFilter(blendFliter);
    blendFliter->setRenderImage(&logo);
    GPUImageTextFilter *textFilter = new GPUImageTextFilter();
    filterGroup->addFilter(textFilter);
    renderer = new GPUImageRenderer(filterGroup);

    renderer->onSurfaceCreated();
    renderer->onSurfaceChanged(image.width / 2, image.height / 2);
    renderer->setRenderImage(&image);
    char info[256];
    std::string tmpStr = "";
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        m_XAngle += 2; frameNums++;
        blendFliter->UpdateMVPMatrix( -0.8, -0.9, 0, m_XAngle, scaleY, scaleY);
        sprintf(info, "Frame: (%d, %d) idd: %d ", image.width, image.height, frameNums);
        textFilter->setMString(std::string(info));
        renderer->onDrawFrame();
        glfwSwapBuffers(window);
    }
    glfwTerminate();

    return EXIT_SUCCESS;
}