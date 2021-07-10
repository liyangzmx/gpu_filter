//
// Created by liyang on 21-7-5.
//

#ifndef ANDROID_PRJ_TEXTUREROTATIONUTIL_H
#define ANDROID_PRJ_TEXTUREROTATIONUTIL_H

#include <stdlib.h>
#include "Rotation.h"

class TextureRotationUtil {
public:
    static const float CUBE[];
    static const float TEXTURE_NO_ROTATION[];
    static const float TEXTURE_ROTATED_90[];
    static const float TEXTURE_ROTATED_180[];
    static const float TEXTURE_ROTATED_270[];

    static float *getRotation(float *output, Rotation rotation, bool flipHorizontal, bool flipVertical);
    static float flip(const float i);
};


#endif //ANDROID_PRJ_TEXTUREROTATIONUTIL_H
