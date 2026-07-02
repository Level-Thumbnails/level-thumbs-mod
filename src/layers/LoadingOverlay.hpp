#pragma once
#include <Geode/Geode.hpp>

class LoadingOverlay : public cocos2d::CCLayer {
protected:
    ~LoadingOverlay();

    bool init() override;
    bool ccTouchBegan(cocos2d::CCTouch* pTouch, cocos2d::CCEvent* pEvent) override;
    void keyBackClicked() override;
    void registerWithTouchDispatcher() override;

    cocos2d::CCLabelBMFont* loadingTextLabel = nullptr;
    cocos2d::CCLayerColor* layerColor = nullptr;
    LoadingCircle* loadingCircle = nullptr;
    int pressCount = 0;

public:
    void changeStatus(const char* status);
    void show();
    void fadeOut();
    static LoadingOverlay* create(const char* status = nullptr);
};