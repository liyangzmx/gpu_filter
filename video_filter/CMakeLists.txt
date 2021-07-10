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

endif(UNIX)