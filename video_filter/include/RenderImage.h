#ifndef __RENDER_IMAGE_H__
#define __RENDER_IMAGE_H__

#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include "stdio.h"
#include "sys/stat.h"
#include "stdint.h"

#define IMAGE_FORMAT_RGBA           0x01
#define IMAGE_FORMAT_NV21           0x02
#define IMAGE_FORMAT_NV12           0x03
#define IMAGE_FORMAT_I420           0x04

#define IMAGE_FORMAT_RGBA_EXT       "RGB32"
#define IMAGE_FORMAT_NV21_EXT       "NV21"
#define IMAGE_FORMAT_NV12_EXT       "NV12"
#define IMAGE_FORMAT_I420_EXT       "I420"

typedef struct _tag_NativeRectF {
    float left;
    float top;
    float right;
    float bottom;

    _tag_NativeRectF() {
        left = top = right = bottom = 0.0f;
    }
} RectF;

typedef struct _RenderImage {
    int width;
    int height;
    int format;
    uint8_t *planes[3];
    int linesize[3];

    _RenderImage() {
        width = 0;
        height = 0;
        format = 0;
        planes[0] = nullptr;
        planes[1] = nullptr;
        planes[2] = nullptr;
        linesize[0] = 0;
        linesize[1] = 0;
        linesize[2] = 0;
    }
} RenderImage;

class RenderImageUtil {
public:
    static void allocRenderImage(RenderImage *image) {
        if (image->height == 0 || image->width == 0) return;

        switch (image->format) {
            case IMAGE_FORMAT_RGBA: {
                image->planes[0] = static_cast<uint8_t *>(malloc(image->width * image->height * 4));
                image->linesize[0] = image->width * 4;
                image->linesize[1] = 0;
                image->linesize[2] = 0;
            }
                break;
            case IMAGE_FORMAT_NV12:
            case IMAGE_FORMAT_NV21: {
                image->planes[0] = static_cast<uint8_t *>(malloc(
                        image->width * image->height * 1.5));
                image->planes[1] = image->planes[0] + image->width * image->height;
                image->linesize[0] = image->width;
                image->linesize[1] = image->width;
                image->linesize[2] = 0;
            }
                break;
            case IMAGE_FORMAT_I420: {
                image->planes[0] = static_cast<uint8_t *>(malloc(
                        image->width * image->height * 1.5));
                image->planes[1] = image->planes[0] + image->width * image->height;
                image->planes[2] = image->planes[1] + (image->width >> 1) * (image->height >> 1);
                image->linesize[0] = image->width;
                image->linesize[1] = image->width / 2;
                image->linesize[2] = image->width / 2;
            }
                break;
            default:
                std::cout << "RenderImageUtil::allocRenderImage do not support the format. Format = " << image->format << std::endl;
                break;
        }
    }

    static void freeRenderImage(RenderImage *image) {
        if (image == nullptr || image->planes[0] == nullptr) return;

        free(image->planes[0]);
        image->planes[0] = nullptr;
        image->planes[1] = nullptr;
        image->planes[1] = nullptr;
    }

    static void copyRenderImage(RenderImage *src, RenderImage *dst) {
//	    std::cout << "RenderImageUtil::copyRenderImage src[w,h,format]=[%d, %d, %d], dst[w,h,format]=[%d, %d, %d]", src->width, src->height, src->format, dst->width, dst->height, dst->format);
//        std::cout << "RenderImageUtil::copyRenderImage src[line0,line1,line2]=[%d, %d, %d], dst[line0,line1,line2]=[%d, %d, %d]", src->linesize[0], src->linesize[1], src->linesize[2], dst->linesize[0], dst->linesize[1], dst->linesize[2]);

        if (src == nullptr || src->planes[0] == nullptr) return;

        if (src->format != dst->format ||
            src->width != dst->width ||
            src->height != dst->height) {
            std::cout << "RenderImageUtil::copyRenderImage invalid params." << std::endl;
            return;
        }

        if (dst->planes[0] == nullptr) allocRenderImage(dst);

        switch (src->format) {
            case IMAGE_FORMAT_I420: {
                // y plane
                if (src->linesize[0] != dst->linesize[0]) {
                    for (int i = 0; i < src->height; ++i) {
                        memcpy(dst->planes[0] + i * dst->linesize[0],
                               src->planes[0] + i * src->linesize[0], dst->width);
                    }
                } else {
                    memcpy(dst->planes[0], src->planes[0], dst->linesize[0] * src->height);
                }

                // u plane
                if (src->linesize[1] != dst->linesize[1]) {
                    for (int i = 0; i < src->height / 2; ++i) {
                        memcpy(dst->planes[1] + i * dst->linesize[1],
                               src->planes[1] + i * src->linesize[1], dst->width / 2);
                    }
                } else {
                    memcpy(dst->planes[1], src->planes[1], dst->linesize[1] * src->height / 2);
                }

                // v plane
                if (src->linesize[2] != dst->linesize[2]) {
                    for (int i = 0; i < src->height / 2; ++i) {
                        memcpy(dst->planes[2] + i * dst->linesize[2],
                               src->planes[2] + i * src->linesize[2], dst->width / 2);
                    }
                } else {
                    memcpy(dst->planes[2], src->planes[2], dst->linesize[2] * src->height / 2);
                }
            }
                break;
            case IMAGE_FORMAT_NV21:
            case IMAGE_FORMAT_NV12: {
                // y plane
                if (src->linesize[0] != dst->linesize[0]) {
                    for (int i = 0; i < src->height; ++i) {
                        memcpy(dst->planes[0] + i * dst->linesize[0],
                               src->planes[0] + i * src->linesize[0], dst->width);
                    }
                } else {
                    memcpy(dst->planes[0], src->planes[0], dst->linesize[0] * src->height);
                }

                // uv plane
                if (src->linesize[1] != dst->linesize[1]) {
                    for (int i = 0; i < src->height / 2; ++i) {
                        memcpy(dst->planes[1] + i * dst->linesize[1],
                               src->planes[1] + i * src->linesize[1], dst->width);
                    }
                } else {
                    memcpy(dst->planes[1], src->planes[1], dst->linesize[1] * src->height / 2);
                }
            }
                break;
            case IMAGE_FORMAT_RGBA: {
                if (src->linesize[0] != dst->linesize[0]) {
                    for (int i = 0; i < src->height; ++i) {
                        memcpy(dst->planes[0] + i * dst->linesize[0],
                               src->planes[0] + i * src->linesize[0], dst->width * 4);
                    }
                } else {
                    memcpy(dst->planes[0], src->planes[0], src->linesize[0] * src->height);
                }
            }
                break;
            default: {
                std::cout << "RenderImageUtil::copyRenderImage do not support the format. Format = " << src->format << std::endl;
            }
                break;
        }

    }

    static void dumpRenderImage(RenderImage *src, const char *fpath, const char *fname) {
        if (src == nullptr || fpath == nullptr || fname == nullptr) return;

        if (access(fpath, 0) == -1) {
            mkdir(fpath, 0666);
        }

        char imgPath[256] = {0};
        const char *ext = nullptr;
        switch (src->format) {
            case IMAGE_FORMAT_I420:
                ext = IMAGE_FORMAT_I420_EXT;
                break;
            case IMAGE_FORMAT_NV12:
                ext = IMAGE_FORMAT_NV12_EXT;
                break;
            case IMAGE_FORMAT_NV21:
                ext = IMAGE_FORMAT_NV21_EXT;
                break;
            case IMAGE_FORMAT_RGBA:
                ext = IMAGE_FORMAT_RGBA_EXT;
                break;
            default:
                ext = "Default";
                break;
        }

        static int index = 0;
        sprintf(imgPath, "%s/IMG_%dx%d_%s_%d.%s", fpath, src->width, src->height, fname,
                index, ext);

        FILE *fp = fopen(imgPath, "wb");

        std::cout << "dumpRenderImage fp=" << fp << ", file=" << imgPath << std::endl;

        if (fp) {
            switch (src->format) {
                case IMAGE_FORMAT_I420: {
                    fwrite(src->planes[0],
                           static_cast<size_t>(src->width * src->height), 1, fp);
                    fwrite(src->planes[1],
                           static_cast<size_t>((src->width >> 1) * (src->height >> 1)), 1,
                           fp);
                    fwrite(src->planes[2],
                           static_cast<size_t>((src->width >> 1) * (src->height >> 1)), 1,
                           fp);
                    break;
                }
                case IMAGE_FORMAT_NV21:
                case IMAGE_FORMAT_NV12: {
                    fwrite(src->planes[0],
                           static_cast<size_t>(src->width * src->height), 1, fp);
                    fwrite(src->planes[1],
                           static_cast<size_t>(src->width * (src->height >> 1)), 1, fp);
                    break;
                }
                case IMAGE_FORMAT_RGBA: {
                    fwrite(src->planes[0],
                           static_cast<size_t>(src->width * src->height * 4), 1, fp);
                    break;
                }
                default: {
                    std::cout << "dumpRenderImage default" << std::endl;
                    break;
                }
            }

            fclose(fp);
            fp = NULL;
        }


    }
};


#endif //__RENDER_IMAGE_H__
