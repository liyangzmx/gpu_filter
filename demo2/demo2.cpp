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
    RenderImage image;
    cv::Mat inputImage = cv::imread("../test.png", 1);
    
    memset(&image, 0, sizeof(image));
    image.format = IMAGE_FORMAT_RGBA;
    image.width = inputImage.cols;
    image.height = inputImage.rows;
    RenderImageUtil::allocRenderImage(&image);
    // memcpy(image.planes[0], inputImage.data, inputImage.cols * inputImage.rows * 4);
    for(int i = 0; i < inputImage.rows; i++) {
        for(int j = 0; j < inputImage.cols; j++) {
            image.planes[0][i * inputImage.cols * 4 + j * 4] = inputImage.data[i * inputImage.cols * 3 + j * 3 + 2];
            image.planes[0][i * inputImage.cols * 4 + j * 4 + 1] = inputImage.data[i * inputImage.cols * 3 + j * 3 + 1];
            image.planes[0][i * inputImage.cols * 4 + j * 4 + 2] = inputImage.data[i * inputImage.cols * 3 + j * 3 + 0];
            image.planes[0][i * inputImage.cols * 4 + j * 4 + 3] = 255;
        }
    }

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow *window = glfwCreateWindow(inputImage.cols / 2, inputImage.rows / 2, __FILE__, NULL, NULL);
    glfwMakeContextCurrent(window);

    GPUImageRenderer *renderer;
    GPUImageFilterGroup *filterGroup = new GPUImageFilterGroup();
    filterGroup->addFilter(new GPUImageRGBFilter(1.0f, 0.0f, 1.0f));
    filterGroup->addFilter(new GPUImageSharpenFilter(0.2f));
    renderer = new GPUImageRenderer(filterGroup);
    renderer->onSurfaceCreated();
    renderer->onSurfaceChanged(image.width / 2, image.height / 2);
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        renderer->setRenderImage(&image);
        renderer->onDrawFrame();
        glfwSwapBuffers(window);
    }
    glfwTerminate();

    return EXIT_SUCCESS;
}