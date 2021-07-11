cmake_minimum_required(VERSION 3.10)

project(GPUImageCpp)

include_directories(/usr/include/)
include_directories(include)

SET(GPUImage_SOURCE_FILES
        GLUtils.cpp
        TextureRotationUtil.cpp
        GPUImageFilter.cpp
        GPUImageFilterGroup.cpp
        GPUImageRenderer.cpp
        PixelBuffer.cpp
        GPUImageInputFilter.cpp
        GPUImageRGBFilter.cpp
        GPUImageTextFilter.cpp
        GPUImageGaussianBlurFilter.cpp
        GPUImageSharpenFilter.cpp
        GPUImageBilateralBlurFilter.cpp
        GPUImageTwoInputFilter.cpp
        GPUImageNormalBlendFilter.cpp
        )

add_library(GPUImage STATIC ${GPUImage_SOURCE_FILES})

if(UNIX)
find_package(PkgConfig REQUIRED)

pkg_check_modules(FREETYPE2 REQUIRED
        IMPORTED_TARGET
        freetype2
        )

target_link_libraries(GPUImage
        PkgConfig::FREETYPE2
)

pkg_check_modules(SDL2 REQUIRED
        IMPORTED_TARGET
        sdl2
        )

pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice
        libavfilter
        libavformat
        libavcodec
        libswresample
        libswscale
        libavutil
        )

add_executable(demo 
        demo/ffplay.c
        demo/cmdutils.c
        )

target_link_libraries(demo 
        GPUImage
        PkgConfig::SDL2
        PkgConfig::LIBAV
        )

add_executable(demo2
        demo2/demo2.cpp
        )

include_directories(/usr/include/opencv4)
target_link_libraries(demo2
        GPUImage
        glfw
        GLESv2
        EGL
        SDL2

        avformat
        avcodec
        avutil
        swresample
        swscale
        opencv_core
        opencv_highgui
        opencv_imgproc
        opencv_imgcodecs
        )

endif(UNIX)