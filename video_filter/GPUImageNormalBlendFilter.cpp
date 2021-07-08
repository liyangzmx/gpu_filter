//
// Created by liyang on 21-7-5.
//

#include "GPUImageNormalBlendFilter.h"

const char GPUImageNormalBlendFilter::NORMAL_BLEND_FRAGMENT_SHADER[] = "varying highp vec2 textureCoordinate;\n"
                                                                       " varying highp vec2 textureCoordinate2;\n"
                                                                       " \n"
                                                                       " uniform sampler2D inputImageTexture;\n"
                                                                       " uniform sampler2D inputImageTexture2;\n"
                                                                       " \n"
                                                                       " void main()\n"
                                                                       " {\n"
                                                                       "     lowp vec4 c2 = texture2D(inputImageTexture, textureCoordinate);\n"
                                                                       "\t lowp vec4 c1 = texture2D(inputImageTexture2, textureCoordinate2);\n"
                                                                       "     \n"
                                                                       "     lowp vec4 outputColor;\n"
                                                                       "     \n"
                                                                       "     outputColor.r = c1.r + c2.r * c2.a * (1.0 - c1.a);\n"
                                                                       "\n"
                                                                       "     outputColor.g = c1.g + c2.g * c2.a * (1.0 - c1.a);\n"
                                                                       "     \n"
                                                                       "     outputColor.b = c1.b + c2.b * c2.a * (1.0 - c1.a);\n"
                                                                       "     \n"
                                                                       "     outputColor.a = c1.a + c2.a * (1.0 - c1.a);\n"
                                                                       "     \n"
                                                                       "     gl_FragColor = outputColor;\n"
                                                                       " }";

GPUImageNormalBlendFilter::GPUImageNormalBlendFilter() : GPUImageTwoInputFilter(NORMAL_BLEND_FRAGMENT_SHADER) {}

