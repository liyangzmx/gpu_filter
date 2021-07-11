//
// Created by liyang on 21-7-2.
//

#include "GPUImageTextFilter.h"
#include "glm/vec2.hpp"

//""
//"attribute vec4 position;\n"
//"attribute vec4 inputTextureCoordinate;\n"
//" \n"
//"varying vec2 textureCoordinate;\n"
//" \n"
//"void main()\n"
//"{\n"
//"    gl_Position = position;\n"
//"    textureCoordinate = inputTextureCoordinate.xy;\n"
//"}";

const char *GPUImageTextFilter::TEXT_VERTEX_SHADER = "attribute vec4 position;// <vec2 pos, vec2 tex>\n"
                                                     "varying vec2 textureCoordinate;\n"
                                                     "void main()\n"
                                                     "{\n"
                                                     "    gl_Position = vec4(position.xy, 0.0, 1.0);\n"
                                                     "    textureCoordinate = position.zw;\n"
                                                     "}";

;const char *GPUImageTextFilter::TEXT_FRAGMENT_SHADER = ""
                                                       " varying highp vec2 textureCoordinate;\n"
                                                       " \n"
                                                       " uniform sampler2D s_textTexture;\n"
                                                       " uniform highp vec3 u_textColor;\n"
                                                       " \n"
                                                       "  void main()\n"
                                                       "  {\n"
                                                       "      highp vec4 textureColor = vec4(1.0, 1.0, 1.0, texture2D(s_textTexture, textureCoordinate).r);\n"
                                                       "      \n"
                                                       "      gl_FragColor = vec4(u_textColor, 1.0) * textureColor;\n"
                                                       "  }\n";
;

GPUImageTextFilter::GPUImageTextFilter() :
        GPUImageFilter(NO_FILTER_VERTEX_SHADER, NO_FILTER_FRAGMENT_SHADER) {
}

void GPUImageTextFilter::onInit() {
    GPUImageFilter::onInit();

    m_TextProgramId = GLUtils::CreateProgram(TEXT_VERTEX_SHADER, TEXT_FRAGMENT_SHADER);
    if (m_TextProgramId) {
        m_SamplerLoc = glGetUniformLocation(m_TextProgramId, "s_textTexture");
    }

    LoadFacesByASCII();
    // Generate VAO Id
    glGenVertexArrays(1, &m_VaoId);
    // Generate VBO Ids and load the VBOs with data
    glGenBuffers(1, &m_VboId);

    glBindVertexArray(m_VaoId);
    glBindBuffer(GL_ARRAY_BUFFER, m_VboId);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glBindBuffer(GL_ARRAY_BUFFER, GL_NONE);
    glBindVertexArray(GL_NONE);
}

GPUImageTextFilter::~GPUImageTextFilter() {
    if (m_TextProgramId) {
        glDeleteProgram(m_TextProgramId);
        glDeleteBuffers(1, &m_VboId);
        glDeleteVertexArrays(1, &m_VaoId);

        std::map<GLint, Character>::const_iterator iter;
        for (iter = m_Characters.begin(); iter != m_Characters.end(); iter++) {
            glDeleteTextures(1, &m_Characters[iter->first].textureID);
        }
    }
}

void GPUImageTextFilter::RenderText(std::string text, GLfloat x, GLfloat y, GLfloat scale,
                                    glm::vec3 color, glm::vec2 viewport) {
    // 激活合适的渲染状态
    glUseProgram(m_TextProgramId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); //禁用byte-alignment限制
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform3f(glGetUniformLocation(m_TextProgramId, "u_textColor"), color.x, color.y, color.z);
    glBindVertexArray(m_VaoId);

    // 对文本中的所有字符迭代
    std::string::const_iterator c;
    x *= viewport.x;
    y *= viewport.y;
    for (c = text.begin(); c != text.end(); c++)
    {
        Character ch = m_Characters[*c];

        GLfloat xpos = x + ch.bearing.x * scale;
        GLfloat ypos = y - (ch.size.y - ch.bearing.y) * scale;

        xpos /= viewport.x;
        ypos /= viewport.y;

        GLfloat w = ch.size.x * scale;
        GLfloat h = ch.size.y * scale;

        w /= viewport.x;
        h /= viewport.y;

        // std::cout << "TextRenderSample::RenderText [xpos,ypos,w,h]=[" << xpos << ", " << ypos << ", " << w << ", " << h << "]" << std::endl;

        // 当前字符的VBO
//        GLfloat vertices[24] = {
//                xpos,     ypos + h,   0.0, 0.0,
//                xpos,     ypos,       0.0, 1.0,
//                xpos + w, ypos,       1.0, 1.0,
//
//                xpos,     ypos + h,   0.0, 0.0,
//                xpos + w, ypos,       1.0, 1.0,
//                xpos + w, ypos + h,   1.0, 0.0
//        };
        // 当前字符的VBO
        GLfloat vertices[6][4] = {
                { xpos,     ypos + h,   0.0, 0.0 },
                { xpos,     ypos,       0.0, 1.0 },
                { xpos + w, ypos,       1.0, 1.0 },

                { xpos,     ypos + h,   0.0, 0.0 },
                { xpos + w, ypos,       1.0, 1.0 },
                { xpos + w, ypos + h,   1.0, 0.0 }
        };

        // 在方块上绘制字形纹理
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ch.textureID);
        glUniform1i(m_SamplerLoc, 0);
        // 更新当前字符的VBO
        glBindBuffer(GL_ARRAY_BUFFER, m_VboId);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        // 绘制方块
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // 更新位置到下一个字形的原点，注意单位是1/64像素
        x += (ch.advance >> 6) * scale; //(2^6 = 64)
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GPUImageTextFilter::LoadFacesByASCII() {
    // FreeType
    FT_Library ft;
    // All functions return a value different than 0 whenever an error occurred
    if (FT_Init_FreeType(&ft))
        std::cout << "TextRenderSample::LoadFacesByASCII FREETYPE: Could not init FreeType Library" << std::endl;

    // Load font as face
    FT_Face face;
    std::string path(DEFAULT_OGL_ASSETS_DIR);
    if (FT_New_Face(ft, (path + "/Antonio-Regular.ttf").c_str(), 0, &face)) {
        std::cout << "TextRenderSample::LoadFacesByASCII FREETYPE: Failed to load font" << std::endl;
    }

    // Set size to load glyphs as
    FT_Set_Pixel_Sizes(face, 0, 96);

    // Disable byte-alignment restriction
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Load first 128 characters of ASCII set
    for (unsigned char c = 0; c < 128; c++)
    {
        // Load character glyph
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
        {
            std::cout << "TextRenderSample::LoadFacesByASCII FREETYTPE: Failed to load Glyph" << std::endl;
            continue;
        }
        // Generate texture
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_LUMINANCE,
                face->glyph->bitmap.width,
                face->glyph->bitmap.rows,
                0,
                GL_LUMINANCE,
                GL_UNSIGNED_BYTE,
                face->glyph->bitmap.buffer
        );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Now store character for later use
        Character character = {
                texture,
                glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
                glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
                static_cast<GLuint>(face->glyph->advance.x)
        };
        m_Characters.insert(std::pair<GLint, Character>(c, character));
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    // Destroy FreeType once we're finished
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

void GPUImageTextFilter::LoadFacesByUnicode(int *unicodeArr, int size) {

}

void GPUImageTextFilter::onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer) {
    GPUImageFilter::onDraw(textureId, cubeBuffer, textureBuffer);
    RenderText(m_String, -0.95f, -0.7f, 1.0f, glm::vec3(1.0, 1.0, 1.0), glm::vec2(m_ViewWidth, m_ViewHeight));
}

void GPUImageTextFilter::onOutputSizeChanged(int width, int height) {
    m_ViewWidth = width;
    m_ViewHeight = height;
}

void GPUImageTextFilter::setMString(const std::string &mString) {
    m_String = mString;
}
