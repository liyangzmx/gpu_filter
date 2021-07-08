cmake_minimum_required(VERSION 3.10)

project(GPUImageCpp)

set(FT_WITH_ZLIB OFF)
set(FT_WITH_BZIP2 OFF)
set(FT_WITH_PNG OFF)
set(FT_WITH_HARFBUZZ)

include_directories(freetype-2.10.0/include/)
add_subdirectory(freetype-2.10.0)

#set(ENV{ANDROID_SDK} ~/Android/Sdk/)
#set(BUILD_JAVA OFF)
#set(BUILD_SHARED_LIBS OFF)
#set(BUILD_FAT_JAVA_LIB oFF)
#set(BUILD_ANDROID_PROJECTS OFF)
#set(BUILD_ANDROID_EXAMPLES OFF)
#set(BUILD_TESTS OFF)
#set(BUILD_PERF_TESTS OFF)
#add_subdirectory(opencv-4.5.2)

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

target_link_libraries(GPUImage
        freetype
        )