#pragma once
#include "Geode/cocos/CCDirector.h"
#include "Geode/cocos/cocoa/CCObject.h"
#include "Geode/cocos/label_nodes/CCLabelBMFont.h"
#include "Geode/cocos/layers_scenes_transitions_nodes/CCLayer.h"
#include "Geode/cocos/menu_nodes/CCMenuItem.h"
#include "Geode/ui/MDTextArea.hpp"
#include "Geode/utils/cocos.hpp"
#include "Geode/utils/file.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

using namespace cocos2d;
using namespace cocos2d::extension;

class ConfirmAlertLayer : public CCLayerColor {
    bool m_scrollable = false;
    std::string m_filePath;
    CCMenu* m_buttonMenu = nullptr;
    int m_controlConnected = -1;
    CCLayer* m_mainLayer = nullptr;
    int m_ZOrder = 0;
    bool m_noElasticity = false;
    bool m_reverseKeyBack = false;
    ScrollingLayer* m_scrollingLayer = nullptr;
    ButtonSprite* m_button2 = nullptr;
    ButtonSprite* m_button1 = nullptr;
    CCMenuItemSpriteExtra* m_button1Extra = nullptr;
    CCMenuItemSpriteExtra* m_button2Extra = nullptr;
    int m_joystickConnected = -1;
    bool m_containsBorder = false;
    bool m_noAction = false;
    float m_timer = 10;
    bool m_understood = false;
    CCLabelBMFont* m_buttonTimer = nullptr;

    geode::Function<void(bool)> m_callback;

    inline void sendAndCleanup(bool alert) {
        m_callback(alert);
        //CCDirector::sharedDirector()->getTouchDispatcher()->setForcePrio(false);
        //CCDirector::sharedDirector()->getTouchDispatcher()->unregisterForcePrio(this);
        removeFromParentAndCleanup(true);
        setTouchEnabled(false);
        setKeypadEnabled(false);
        setKeyboardEnabled(false);
    }

    bool init(
        char const* title, std::string caption,
        char const* button1, char const* button2,
        float width, bool border,
        float height
    ) {
        if (!cocos2d::CCLayerColor::initWithColor(ccc4(0, 0, 0, 150))) return false;
        m_containsBorder = border;

        float checkboxMargin = 20;

        // handle touch prio
        cocos2d::CCTouchDispatcher::get()->registerForcePrio(this, 2, 0);
        //this->registerWithTouchDispatcher();

        //this->setTouchPriority(-999);
        handleTouchPriority(this, true);

        setTouchEnabled(true);
        setKeypadEnabled(true);
        setKeyboardEnabled(true);
        m_reverseKeyBack = false;

        auto windowSize = CCDirector::sharedDirector()->getWinSize();

        m_mainLayer = cocos2d::CCLayer::create();
        addChild(m_mainLayer);

        TextArea* textArea = nullptr;
        MDTextArea* markdownTextArea = nullptr;
        if (!m_containsBorder) {
            if (!m_scrollable) {
                textArea = TextArea::create(
                    std::move(caption), "chatFont.fnt", 1.0, width - 60.0f, CCPointMake(0.5, 0.5), 20.0, false, 0
                );
                m_mainLayer->addChild(textArea, 3);
                height = textArea->m_obRect.size.height + 120;
                height = std::max(height, 140.0f);
            } else {
                auto fileop = file::readString(Mod::get()->getResourcesDir() / m_filePath.c_str());
                markdownTextArea = MDTextArea::create(
                    std::move(fileop).unwrapOrDefault(),
                    {width - 60.f, 100.f}
                );
                m_mainLayer->addChild(markdownTextArea, 3);
                height = markdownTextArea->getContentHeight() + 120;
                height = std::max(height, 140.f);
            }
        }

        auto scale9 = NineSlice::create("square01_001.png", CCRectMake(0, 0, 94, 94));
        scale9->setContentSize(CCSizeMake(width, height+checkboxMargin));
        auto scale9Pos = CCPointMake(windowSize.width*0.5, windowSize.height*0.5);
        scale9->setPosition(scale9Pos);
        m_mainLayer->addChild(scale9, 1);

        auto titleText = CCLabelBMFont::create(title, "goldFont.fnt");
        titleText->setAnchorPoint(CCPointMake(0.5, 1.0));
        titleText->setPosition(
            windowSize.width * 0.5f, (windowSize.height - height) * 0.5f + height - 15 + checkboxMargin / 2
        );
        titleText->setScale(0.9);
        m_mainLayer->addChild(titleText, 3);

        if (!m_containsBorder) {
            if (!m_scrollable) textArea->setPosition(scale9Pos + CCPointMake(0, 5 + checkboxMargin/2));
            else markdownTextArea->setPosition(scale9Pos + CCPointMake(0, 5 + checkboxMargin/2));
        }

        m_buttonMenu = CCMenu::create();
        m_mainLayer->addChild(m_buttonMenu, 2);


        m_button1 = ButtonSprite::create(button1, 90, 0, 1.0, false);
        auto button1Menu = CCMenuItemSpriteExtra::create(
            m_button1, nullptr, this, menu_selector(ConfirmAlertLayer::onBtn1)
        );
        m_button1Extra = button1Menu;
        m_buttonMenu->addChild(button1Menu);


        if (!button2) m_noAction = true;
        else {
            m_button2 = ButtonSprite::create(button2, 90, 0, 1.0, false);
            auto button2Menu = CCMenuItemSpriteExtra::create(
                m_button2, nullptr, this, menu_selector(ConfirmAlertLayer::onBtn2)
            );
            m_button2Extra = button2Menu;
            m_buttonMenu->addChild(button2Menu);
        }

        float menuLength = m_button1->getContentSize().width;
        menuLength += m_button2->getContentSize().width;
        auto menuCount = m_buttonMenu->getChildren()->count();
        if (menuCount > 1) {
            float padding = (width - menuLength) * 0.5f;
            padding = std::min(padding, 15.0f);
            m_buttonMenu->alignItemsHorizontallyWithPadding(padding);
        }

        m_buttonMenu->setPosition(
            CCPointMake(windowSize.width * 0.5, (windowSize.height - height) * 0.5 + 30.0 - checkboxMargin/2)
        );

        // timed label implementation thing
        auto readThisMenu = CCMenu::create();

        std::string readThisText = "I understand the consequences of this action.";
        if (m_scrollable) readThisText = "I have read and understood the rules.";
        auto readThisLabel = CCLabelBMFont::create(readThisText.c_str(), "bigFont.fnt");
        auto readThisToggle = CCMenuItemExt::createToggler(
            CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
            CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"), [this](CCObject* sender) {
                m_understood = !m_understood;
            }
        );

        readThisToggle->setScale(0.5f);
        readThisLabel->limitLabelWidth(241.f, 0.5f, 0.0001f);

        // offset
        readThisLabel->setPositionX(15.f);
        readThisToggle->setPositionX(-readThisLabel->getScaledContentWidth() / 2);

        readThisMenu->addChild(readThisLabel);
        readThisMenu->addChild(readThisToggle);

        readThisMenu->setPosition(windowSize.width / 2, (windowSize.height - height) * 0.5f + 33.f + checkboxMargin);

        m_mainLayer->addChild(readThisMenu);
        readThisMenu->setZOrder(1); // this is stupid, and stupid, and stupid

        if (m_scrollable) m_timer = 30;

        this->scheduleUpdate();

        return true;
    }

    void update(float dt) override {
        if (!m_button2Extra) return;
        bool buttonEnabled = false;
        bool showTimer = true;

        if (floor(m_timer) > 0) {
            m_timer -= dt;
        }

        // unfortunately have to do this so it doesnt flicker
        if (floor(m_timer) <= 0) {
            buttonEnabled = true;
            showTimer = false;
        }

        if (!m_understood) buttonEnabled = false;

        if (buttonEnabled) {
            if (m_buttonTimer) m_buttonTimer->setVisible(false);
            m_button2Extra->setEnabled(true);
            m_button2->setOpacity(255);
            m_button2->setColor({255, 255, 255});
            m_button2->setCascadeOpacityEnabled(false);
            m_button2->setCascadeColorEnabled(false);
        } else {
            if (!m_buttonTimer) {
                m_buttonTimer = CCLabelBMFont::create(fmt::format("{}", floor(m_timer)).c_str(), "bigFont.fnt");
                m_button2->addChild(m_buttonTimer);
                m_buttonTimer->setPosition(m_button2->m_label->getPosition());
                m_buttonTimer->setZOrder(999);
                m_buttonTimer->setScale(0.8);
            } else {
                m_buttonTimer->setVisible(showTimer);
                m_buttonTimer->setString(fmt::format("{}", floor(m_timer)).c_str());
            }
            m_button2Extra->setEnabled(false);
            m_button2->setCascadeOpacityEnabled(true);
            m_button2->setOpacity(150);
            m_button2->setCascadeColorEnabled(true);
            m_button2->setColor({150, 150, 150});
        }
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        return true;
    }

    void keyBackClicked() override {
        setKeypadEnabled(false);
        setKeyboardEnabled(false);
        sendAndCleanup(m_reverseKeyBack);
    }

    void registerWithTouchDispatcher() override {
        CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -500, true);
    }

    void keyDown(enumKeyCodes keyCode, double timestamp) override {
        if (keyCode == 0x3ec) sendAndCleanup(true);
        else if (keyCode == KEY_Space && !m_noAction) return;
        else CCLayer::keyDown(keyCode, timestamp);
    }

    void onBtn1(cocos2d::CCObject*) {
        sendAndCleanup(false);
    }

    void onBtn2(cocos2d::CCObject*) {
        sendAndCleanup(true);
    }

public:
    CCNode* m_scene = nullptr;

    static ConfirmAlertLayer* create(
        geode::Function<void(bool)> callback, char const* title, std::string caption, char const* button1,
        char const* button2
    ) {
        return create(std::move(callback), title, std::move(caption), button1, button2, 350.0, false, 0.0);
    }

    static ConfirmAlertLayer* create(
        geode::Function<void(bool)> callback, char const* title, std::string caption, char const* button1,
        char const* button2, float width
    ) {
        return create(std::move(callback), title, std::move(caption), button1, button2, width, false, 0.0);
    }

    static ConfirmAlertLayer* createRulesPopup(
        geode::Function<void(bool)> callback, char const* title, std::string filePath, char const* button1,
        char const* button2
    ) {
        return create(std::move(callback), title, "", button1, button2, 350.0, false, 0.0, true, std::move(filePath));
    }

    static ConfirmAlertLayer* create(
        geode::Function<void(bool)> callback, char const* title, std::string caption, char const* button1,
        char const* button2, float width, bool border, float height, bool scrollable = false, std::string filePath = ""
    ) {
        auto ret = new ConfirmAlertLayer();
        ret->m_callback = std::move(callback);
        ret->m_scrollable = scrollable;
        ret->m_filePath = std::move(filePath);
        if (ret->init(title, std::move(caption), button1, button2, width, border, height)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void show() {
        if (!m_noElasticity) {
            m_mainLayer->setScale(0.1);
            auto scaleTo = CCScaleTo::create(0.5, 1);
            auto elasticOut = CCEaseElasticOut::create(scaleTo, 0.6);
            m_mainLayer->runAction(elasticOut);
        }

        int zOrder;
        auto runningScene = m_scene;
        if (!runningScene) {
            runningScene = CCDirector::sharedDirector()->getRunningScene();
            m_ZOrder = CCDirector::sharedDirector()->getRunningScene()->getHighestChildZ() + 1;
        }
        m_ZOrder = std::max(105, m_ZOrder);
        runningScene->addChild(this, m_ZOrder);

        if (!m_noElasticity) {
            auto oldOpacity = getOpacity();
            setOpacity(0);
            runAction(CCFadeTo::create(0.14, oldOpacity));
        }

        setVisible(true);
    }
};
