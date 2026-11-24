#pragma once

#include <Geode/utils/web.hpp>
#include "../managers/AuthManager.hpp"
#include "../managers/ThumbnailManager.hpp"

using namespace geode::prelude;

class ThumbnailPopup : public Popup {
protected:
    std::unordered_set<Ref<CCTouch>> m_touches;
    float m_initialDistance = 0;
    float m_initialScale = 0;
    CCPoint m_touchMidPoint;
    int m_levelID = 0;
    float m_maxHeight = 220;

    bool m_isPreview = false;
    bool m_isReplacement = false;
    bool wasZooming = false;
    bool m_usedSwap = false;

    TaskHolder<ThumbnailManager::FetchResult> m_downloadListener;
    TaskHolder<web::WebResponse> m_infoListener;
    TaskHolder<AuthManager::UploadResult> m_uploadListener;

    LoadingCircle* m_loadingCircle = LoadingCircle::create();
    CCMenuItemSpriteExtra* m_downloadBtn = nullptr;
    CCMenuItemSpriteExtra* m_infoBtn = nullptr;
    CCMenuItemToggler* m_thumbInfoBtn = nullptr;
    CCClippingNode* m_clippingNode = nullptr;
    CCSprite* m_thumbnail = nullptr;
    Ref<CCTexture2D> m_swappedTexture = nullptr;
    CCLabelBMFont* m_theFunny = nullptr;

    std::string m_previewFileName;
    std::string m_submissionNote;
    std::string m_extraNote;

    void runSubmissionLogic();

    void loadThumbnailInfo();
    void onDownloadSuccess(Ref<CCTexture2D> const& texture);
    void onDownloadError(std::string const& error);

    bool init(int id);
    void onDownload(CCObject* sender);
    void openDiscordServerPopup(CCObject* sender);
    void onOpenFolder(CCObject* sender);
    void onEditNote(CCObject* sender);
    void recenter(CCObject* sender);
    void enableSwapping();

    bool ccTouchBegan(cocos2d::CCTouch* pTouch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* pTouch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* pTouch, cocos2d::CCEvent* event) override;

    bool scrollWheel(float y, float x) override;

public:
    static ThumbnailPopup* create(int id, bool screenshotPreview = false);
    static ThumbnailPopup* create(int id, std::string filename, std::string note = "");
};