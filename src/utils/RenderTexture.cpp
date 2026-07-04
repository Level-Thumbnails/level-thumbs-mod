#include "RenderTexture.hpp"

RenderTexture::RenderTexture(uint32_t width, uint32_t height) : m_width(width), m_height(height) {
    // create texture
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenTextures(1, &m_texture);

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA,
        static_cast<GLsizei>(m_width),
        static_cast<GLsizei>(m_height),
        0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
    );

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_oldFBO);

    // create FBO
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_oldFBO);
}

RenderTexture::~RenderTexture() {
    this->end();
    glDeleteTextures(1, &m_texture);
    glDeleteFramebuffers(1, &m_fbo);
}

void RenderTexture::begin() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_oldFBO);

    auto view = cocos2d::CCEGLView::get();
    auto director = cocos2d::CCDirector::get();
    auto winSize = director->getWinSize();

    m_oldScale = cocos2d::CCSize{ view->m_fScaleX, view->m_fScaleY };
    m_oldResolution = view->getDesignResolutionSize();

    auto displayFactor = geode::utils::getDisplayFactor();
    view->m_fScaleX = static_cast<float>(m_width) / winSize.width / displayFactor;
    view->m_fScaleY = static_cast<float>(m_height) / winSize.height / displayFactor;

    auto aspectRatio = static_cast<float>(m_width) / m_height;
    auto newRes = cocos2d::CCSize{ std::round(320.f * aspectRatio), 320.f };

    director->m_obWinSizeInPoints = newRes;
    m_oldScreenSize = view->m_obScreenSize;
    view->m_obScreenSize = cocos2d::CCSize{ static_cast<float>(m_width), static_cast<float>(m_height) };
    view->setDesignResolutionSize(newRes.width, newRes.height, kResolutionExactFit);

    glViewport(0, 0, m_width, m_height);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RenderTexture::end() {
    if (m_oldFBO != -1) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_oldFBO);
        m_oldFBO = -1;
    }
    if (m_oldScale.width != 0 && m_oldScale.height != 0) {
        auto view = cocos2d::CCEGLView::get();
        auto director = cocos2d::CCDirector::get();
        view->m_fScaleX = m_oldScale.width;
        view->m_fScaleY = m_oldScale.height;
        director->m_obWinSizeInPoints = m_oldResolution;
        view->m_obScreenSize = m_oldScreenSize;
        view->setDesignResolutionSize(m_oldResolution.width, m_oldResolution.height, kResolutionExactFit);
        director->setViewport();
        m_oldScale = cocos2d::CCSize{0, 0};
    }
}

std::unique_ptr<uint8_t[]> RenderTexture::getData() const {
    if (!m_texture) {
        return nullptr;
    }

    auto data = std::make_unique<uint8_t[]>(m_width * m_height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, data.get());

    // force full opacity for all pixels
    for (size_t i = 0; i < m_width * m_height; ++i) {
        data[i * 4 + 3] = 255;
    }

    return data;
}
