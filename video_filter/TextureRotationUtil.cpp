//
// Created by liyang on 21-7-5.
//

#include "TextureRotationUtil.h"

const float TextureRotationUtil::TEXTURE_NO_ROTATION[] = {
        0.0f,  1.0f,        // TexCoord 1
        0.0f,  0.0f,        // TexCoord 0
        1.0f,  1.0f,        // TexCoord 2
        1.0f,  0.0f,        // TexCoord 3
};

const float TextureRotationUtil::TEXTURE_ROTATED_90[] = {
        1.0f,  0.0f,        // TexCoord 3
        0.0f,  0.0f,        // TexCoord 0
        1.0f,  1.0f,        // TexCoord 2
        0.0f,  1.0f,        // TexCoord 1
};

const float TextureRotationUtil::TEXTURE_ROTATED_180[] = {
        0.0f,  0.0f,        // TexCoord 0
        0.0f,  1.0f,        // TexCoord 1
        1.0f,  0.0f,        // TexCoord 3
        1.0f,  1.0f,        // TexCoord 2
};

const float TextureRotationUtil::TEXTURE_ROTATED_270[] = {
        0.0f,  0.0f,        // TexCoord 0
        1.0f,  0.0f,        // TexCoord 3
        0.0f,  1.0f,        // TexCoord 1
        1.0f,  1.0f,        // TexCoord 2
};

const float TextureRotationUtil::CUBE[] = {
        -1.0f, -1.0f,   // 1
        -1.0f, 1.0f,    // 0
        1.0f, -1.0f,    // 2
        1.0f, 1.0f,     // 3
};

float *TextureRotationUtil::getRotation(float *output, Rotation rotation, bool flipHorizontal, bool flipVertical)  {
    switch (rotation) {
        case ROTATION_90:
            for(int i = 0; i < 8; i++)
                output[i] = TEXTURE_ROTATED_90[i];
            break;
        case ROTATION_180:
            for(int i = 0; i < 8; i++)
                output[i] = TEXTURE_ROTATED_180[i];
            break;
        case ROTATION_270:
            for(int i = 0; i < 8; i++)
                output[i] = TEXTURE_ROTATED_270[i];
            break;
        case NORMAL:
        default:
            for(int i = 0; i < 8; i++)
                output[i] = TEXTURE_NO_ROTATION[i];
            break;
    }
    if (flipHorizontal) {
        for (int i = 0; i < 8; ++i) {
            if((i % 2) == 0) {
                output[i] = flip(output[i]);
            }
        }
    }
    if (flipVertical) {
        for (int i = 0; i < 8; ++i) {
            if((i % 2) == 1) {
                output[i] = flip(output[i]);
            }
        }
    }

    return output;
}

float TextureRotationUtil::flip(const float i) {
    if (i == 0.0f) {
        return 1.0f;
    }
    return 0.0f;
}