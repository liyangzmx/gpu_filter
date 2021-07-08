//
// Created by liyang on 21-6-25.
//

#include "TextureRotationUtil.h"
#include "GPUImageFilterGroup.h"

GPUImageFilterGroup::GPUImageFilterGroup()
        : GPUImageFilter(true),
          m_FramebuffersLen(0),
          m_FramebufferTexturesLen(0),
          m_Framebuffers(nullptr),
          m_FramebufferTextures(nullptr) {
}

GPUImageFilterGroup::GPUImageFilterGroup(const std::vector<GPUImageFilter *> filters)
        : GPUImageFilter(true),
          m_FramebuffersLen(0),
          m_FramebufferTexturesLen(0),
          m_Framebuffers(nullptr),
          m_FramebufferTextures(nullptr) {
    m_Filters.assign(filters.begin(), filters.end());
    if (m_Filters.size() != 0) {
        updateMergedFilters();
    }
}

GPUImageFilterGroup::~GPUImageFilterGroup() {
    destroyFramebuffers();
    for (auto filter : m_Filters) {
        delete filter;
    }
    m_Filters.clear();
}

void GPUImageFilterGroup::addFilter(GPUImageFilter *filter) {
    if (filter == nullptr) {
        return;
    }
    m_Filters.push_back(filter);
    updateMergedFilters();
}

void GPUImageFilterGroup::onInit() {
    GPUImageFilter::onInit();
    for (auto filter : m_Filters) {
        (*filter).ifNeedInit();
    }
}

void GPUImageFilterGroup::destroyFramebuffers() {
    if (m_FramebufferTextures != nullptr) {
        glDeleteTextures(m_FramebufferTexturesLen, m_FramebufferTextures);
        m_FramebufferTextures = nullptr;
    }
    if (m_Framebuffers != nullptr) {
        glDeleteFramebuffers(m_FramebuffersLen, m_Framebuffers);
        m_Framebuffers = nullptr;
    }
}

void GPUImageFilterGroup::onOutputSizeChanged(const int width, const int height) {
    GPUImageFilter::onOutputSizeChanged(width, height);
    if (m_Framebuffers != nullptr) {
        destroyFramebuffers();
    }
    int size = m_Filters.size();

    for (int i = 0; i < size; i++) {
        m_Filters[i]->onOutputSizeChanged(width, height);
    }
    if (m_MergedFilters.size() > 0) {
        int size = m_MergedFilters.size();
        m_FramebuffersLen = size - 1;
        m_FramebufferTexturesLen = size - 1;
        if (m_FramebuffersLen > 0) {
            m_Framebuffers = (GLuint *) malloc(sizeof(GLuint) * m_FramebuffersLen);
        }
        if (m_FramebufferTexturesLen > 0) {
            m_FramebufferTextures = (GLuint *) malloc(sizeof(GLuint) * m_FramebufferTexturesLen);
        }
        for (int i = 0; i < size - 1; i++) {
            glGenFramebuffers(1, &m_Framebuffers[i]);
            glGenTextures(1, &m_FramebufferTextures[i]);
            glBindTexture(GL_TEXTURE_2D, m_FramebufferTextures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameterf(GL_TEXTURE_2D,
                            GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D,
                            GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameterf(GL_TEXTURE_2D,
                            GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffers[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, m_FramebufferTextures[i], 0);

            glBindTexture(GL_TEXTURE_2D, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
}

void GPUImageFilterGroup::onInitialized() {
    GPUImageFilter::onInitialized();
}

std::vector<GPUImageFilter *> &GPUImageFilterGroup::getMergedFilters() {
    return m_MergedFilters;
}

std::vector<GPUImageFilter *> &GPUImageFilterGroup::getFilters() {
    return m_Filters;
}

void
GPUImageFilterGroup::onDraw(int textureId, const float *cubeBuffer, const float *textureBuffer) {
    runPendingOnDrawTasks();
//    if (!isInitialized() || m_Framebuffers == nullptr || m_FramebufferTextures == nullptr) {
    if (!isInitialized()) {
        return;
    }
    int size = 0;
    if (m_MergedFilters.size() != 0) {
        size = m_MergedFilters.size();
    }
    int previousTexture = textureId;
    for (int i = 0; i < size; i++) {
        GPUImageFilter *filter = m_MergedFilters[i];
        bool isNotLast = i < size - 1;
        if (isNotLast) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffers[i]);
            glClearColor(0, 0, 0, 0);
        }
        if (i == 0) {
            filter->onDraw(previousTexture, cubeBuffer, textureBuffer);
        } else if (i == size - 1) {
            filter->onDraw(previousTexture, TextureRotationUtil::CUBE,
                           (size % 2 == 0) ? TextureRotationUtil::TEXTURE_ROTATED_180
                                           : TextureRotationUtil::TEXTURE_NO_ROTATION);
        } else {
            filter->onDraw(previousTexture, TextureRotationUtil::CUBE,
                           TextureRotationUtil::TEXTURE_NO_ROTATION);
        }
        if (isNotLast) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            previousTexture = m_FramebufferTextures[i];
        }
    }
}

void GPUImageFilterGroup::updateMergedFilters() {
    if (m_Filters.size() == 0) {
        return;
    }
    if (m_MergedFilters.size() != 0) {
//        for (auto filter : m_MergedFilters) {
//            delete filter;
//        }
        m_MergedFilters.clear();
    }

    for (auto tmpFilter : m_Filters) {
        if (tmpFilter->isMIsGroupFilter()) {
            GPUImageFilterGroup *tmp = static_cast<GPUImageFilterGroup *>(tmpFilter);
            if (tmp != nullptr) {
                tmp->updateMergedFilters();
                GPUImageFilterGroup *filterGroup = (GPUImageFilterGroup *) (tmpFilter);
                std::vector < GPUImageFilter * > filters = filterGroup->getMergedFilters();
                for (auto filter : filters) {
                    m_MergedFilters.push_back(filter);
                }
            }
        } else {
            m_MergedFilters.push_back(tmpFilter);
        }
    }
}