//
// Created by liyang on 21-7-2.
//

#ifndef ANDROID_PRJ_GPUIMAGETEXTFILTER_H
#define ANDROID_PRJ_GPUIMAGETEXTFILTER_H

#include "GPUImageFilter.h"
#include <map>
#include <glm/detail/type_mat4x4.hpp>

#include <freetype/ftglyph.h>

#define DEFAULT_OGL_ASSETS_DIR "../"

struct Character {
    GLuint textureID;   // ID handle of the glyph texture
    glm::ivec2 size;    // Size of glyph
    glm::ivec2 bearing;  // Offset from baseline to left/top of glyph
    GLuint advance;    // Horizontal offset to advance to next glyph
};

class GPUImageTextFilter : public GPUImageFilter {
public:
    GPUImageTextFilter();
    virtual ~GPUImageTextFilter();
    void RenderText(std::string text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color, glm::vec2 viewport);
    void LoadFacesByASCII();
    void LoadFacesByUnicode(int *unicodeArr, int size);

    virtual void onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer);
    virtual void onOutputSizeChanged(int width, int height);
    virtual void onInit();

    static const char *TEXT_VERTEX_SHADER;
    static const char *TEXT_FRAGMENT_SHADER;
private:
    std::map<GLint, Character> m_Characters;
    GLuint m_TextProgramId;
    GLint m_SamplerLoc;
    int m_ViewWidth = 1280;
    int m_ViewHeight = 720;
    GLuint m_VaoId;
    GLuint m_VboId;
    std::string m_String = "";
public:
    void setMString(const std::string &mString);
};


#endif //ANDROID_PRJ_GPUIMAGETEXTFILTER_H
