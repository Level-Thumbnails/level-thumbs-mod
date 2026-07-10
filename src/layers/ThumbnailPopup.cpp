#include "ThumbnailPopup.hpp"
#include <chrono>
#include <argon/argon.hpp>
#include <fmt/format.h>
#include <Geode/Geode.hpp>
#include <Geode/cocos/textures/CCTextureCache.h>
#include <Geode/ui/Button.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/utils/web.hpp>
#include "ConfirmAlertLayer.hpp"
#include "Geode/utils/general.hpp"
#include "LoadingOverlay.hpp"
#include "../utils/UrlEncode.hpp"

using namespace geode::prelude;

void ThumbnailPopup::onDownload(CCObject* sender) {
    CCApplication::sharedApplication()->openURL(ThumbnailManager::getThumbnailUrl(m_levelID).c_str());
}

void ThumbnailPopup::onOpenFolder(CCObject* sender) {
    file::openFolder(Mod::get()->getSaveDir());
    clipboard::write(fmt::to_string(this->m_levelID));
    Notification::create("Copied ID to clipboard.", nullptr)->show();
}

class EditNotePopup : public Popup, public TextInputDelegate {
public:
    static EditNotePopup* create(std::string currentNote, Function<void(std::string)> onSubmit) {
        auto ret = new EditNotePopup();
        if (ret->init(std::move(currentNote), std::move(onSubmit))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void textChanged(CCTextInputNode* input) override {
        m_currentNote = input->getString();
        m_charCountLabel->setString(fmt::format("{}/300", m_currentNote.size()).c_str());
    }

    bool init(std::string currentNote, Function<void(std::string)> onSubmit) {
        if (!Popup::init(300, 200, "square01_001.png"))
            return false;

        this->setTitle("Edit submission note");
        static_cast<AnchorLayoutOptions*>(m_title->getLayoutOptions())->setOffset({0, -25});

        m_currentNote = std::move(currentNote);
        m_onSubmit = std::move(onSubmit);

        auto label = CCLabelBMFont::create(
            "Attach an optional note to your submission.\nIt will be visible to thumbnail moderators.",
            "chatFont.fnt",
            280.f, kCCTextAlignmentCenter
        );
        label->setScale(0.9f);
        m_mainLayer->addChildAtPosition(label, Anchor::Top, {0, -60});

        auto inputBg = NineSlice::create("square02_001.png");
        inputBg->setContentSize({250, 60});
        inputBg->setOpacity(128);
        m_mainLayer->addChildAtPosition(inputBg, Anchor::Bottom, {0, 85});

        auto charCount = CCLabelBMFont::create(fmt::format("{}/300", m_currentNote.size()).c_str(), "chatFont.fnt");
        charCount->setScale(0.5f);
        charCount->setOpacity(128);
        charCount->setAnchorPoint({1.f, 0.f});
        m_mainLayer->addChildAtPosition(charCount, Anchor::Bottom, {120, 58});
        m_charCountLabel = charCount;

        auto textInput = CCTextInputNode::create(310, 60, "Enter submission note...", "Thonburi", 24, nullptr);
        textInput->setAllowedChars(getCommonFilterAllowedChars(CommonFilter::Any));
        textInput->setMaxLabelLength(300);
        textInput->setScale(0.7f);
        textInput->setAnchorPoint({0.f, 0.f});
        textInput->setLabelPlaceholderColor({200, 200, 200});
        textInput->setDelegate(this);
        textInput->addTextArea(TextArea::create("", "chatFont.fnt", 1.f, 280.f, {0.5f, 0.5f}, 20.f, true));
        m_mainLayer->addChildAtPosition(textInput, Anchor::Bottom, {0, 85});
        m_textInput = textInput;

        textInput->setString(m_currentNote);

        // i love cocos
        auto hiddenButton = CCMenuItemExt::create([this](CCMenuItem*) {
            m_textInput->onClickTrackNode(!m_textInput->m_selected);
        });
        hiddenButton->setContentSize(inputBg->getContentSize());
        m_buttonMenu->addChildAtPosition(hiddenButton, Anchor::Bottom, {0, 85});

        auto submitBtn = Button::createWithNode(
            ButtonSprite::create("Submit"),
            [this](auto) {
                if (m_onSubmit) m_onSubmit(std::move(m_currentNote));
                this->onClose(nullptr);
            }
        );

        auto cancelBtn = Button::createWithNode(
            ButtonSprite::create("Cancel"),
            [this](auto) {
                this->onClose(nullptr);
            }
        );

        m_mainLayer->addChildAtPosition(cancelBtn, Anchor::Bottom, {-60, 32});
        m_mainLayer->addChildAtPosition(submitBtn, Anchor::Bottom, {60, 32});

        m_mainLayer->updateLayout();

        return true;
    }

private:
    Function<void(std::string)> m_onSubmit;
    std::string m_currentNote;
    CCTextInputNode* m_textInput = nullptr;
    CCLabelBMFont* m_charCountLabel = nullptr;
};

void ThumbnailPopup::onEditNote(CCObject* sender) {
    EditNotePopup::create(
        m_extraNote,
        [self = WeakRef(this)](std::string newNote){
            if (auto s = self.lock()) {
                s->m_extraNote = std::move(newNote);
                log::info("X-Submission-Note: {}m={}", s->m_submissionNote, s->m_extraNote);
            }
        }
    )->show();
}

void ThumbnailPopup::openDiscordServerPopup(CCObject* sender) {
    if (m_isPreview){
        createQuickPopup(
            "Confirmation",
            "Are you sure you want to submit?",
            "No", "Yes",
            [this](auto, bool btn2){
                if (!Mod::get()->getSavedValue<bool>("showed-rules") && btn2){
                    ConfirmAlertLayer::createRulesPopup(
                        [this](bool btn2){
                            if (btn2) {
                                runSubmissionLogic();
                                Mod::get()->setSavedValue<bool>("showed-rules", true);
                            }
                        },
                        "Submission Rules",
                        "submission_rules.md",
                        "OK","Submit"
                    )->show();
                } else {
                    if (btn2) runSubmissionLogic();
                }
            }
        );
    } else {
        createQuickPopup(
            "No thumbnail!",
            "This level seems to not have a <cj>Thumbnail</c>...\n"
            "Don't worry, you can submit a thumbnail yourself! Open the level and click the thumbnail button in the pause menu.",
            "No Thanks", "Join Discord",
            [](auto, bool btn2) {
                if (btn2) {
                    CCApplication::sharedApplication()->openURL("https://discord.gg/GuagJDsqds");
                }
            }
        );
    }
}
void ThumbnailPopup::runSubmissionLogic() {
    if (m_isReplacement && m_extraNote.empty() && !AuthManager::get().roleIsEqualOrAbove(ThumbnailRole::MODERATOR)) {
        FLAlertLayer::create(nullptr,"Error!","<cr>You must add a </c><cy>submission note</c><cr> when submitting a replacement!</c>","OK",nullptr,400)->show();
        return;
    }

    StringBuffer noteEncodeBuffer;
    urlEncodeAppend(noteEncodeBuffer, m_extraNote);
    auto load = LoadingOverlay::create("Logging in...");
    load->show();
    m_uploadListener.spawn(
        AuthManager::get().uploadThumbnail(
            m_previewFileName, m_levelID, fmt::format("{}m={}", m_submissionNote, noteEncodeBuffer.str()),
            [load](ZStringView progress) {
                queueInMainThread([load, progress] {
                    load->changeStatus(progress.c_str());
                });
            }
        ),
        [load](auto res){
            load->fadeOut();
            if (res.isOk()) {
                FLAlertLayer::create(nullptr, "Success!", res.unwrapOrDefault(), "OK", nullptr, 400)->show();
            } else {
                FLAlertLayer::create(nullptr, "Error!", res.unwrapErr(), "OK", nullptr, 400)->show();
            }
        }
    );
}


bool ThumbnailPopup::init(int id) {
    if (!Popup::init(395.f, 225.f, "GJ_square05.png"))
        return false;

    m_noElasticity = false;
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    this->setID("ThumbnailPopup");

    NineSlice* border = NineSlice::create("GJ_square07.png");
    border->setContentSize(m_bgSprite->getContentSize());
    border->setPosition(m_bgSprite->getPosition());
    border->setZOrder(2);

    CCLayerColor* mask = CCLayerColor::create({255, 255, 255});
    mask->setContentSize({391, 220});
    mask->setPosition({m_bgSprite->getContentSize().width/2 - 391.f/2, m_bgSprite->getContentSize().height/2 - 220.f/2});

    m_bgSprite->setColor({50,50,50});

    m_clippingNode = CCClippingNode::create();
    m_clippingNode->setContentSize(m_bgSprite->getContentSize());
    m_clippingNode->setStencil(mask);
    m_clippingNode->setZOrder(1);

    m_mainLayer->addChild(border);
    m_mainLayer->addChild(m_clippingNode);

    CCSprite* downloadSprite = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
    m_downloadBtn = CCMenuItemSpriteExtra::create(downloadSprite, this, menu_selector(ThumbnailPopup::onDownload));
    m_downloadBtn->setEnabled(true);
    m_downloadBtn->setVisible(!m_isPreview);
    m_downloadBtn->setColor({125,125,125});

    m_downloadBtn->setPosition({m_mainLayer->getContentSize().width - 5, 5});

    m_buttonMenu->addChild(m_downloadBtn);

    CCSprite* infoBtn = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    CCSprite* infoBtnDark = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    infoBtnDark->setColor({100,100,100});
    m_thumbInfoBtn = CCMenuItemExt::createToggler(infoBtnDark, infoBtn, [this](auto self){
        if (auto info = m_mainLayer->getChildByID("thumbnail-info")) info->setVisible(!self->isOn());
        Mod::get()->setSavedValue<bool>("show-info", !self->isOn());
    });
    m_thumbInfoBtn->toggle(Mod::get()->getSavedValue<bool>("show-info"));
    m_thumbInfoBtn->setVisible(!m_isPreview);

    m_thumbInfoBtn->setPosition({m_mainLayer->getContentSize().width - 5, 220});

    m_buttonMenu->addChild(m_thumbInfoBtn);

    CCSprite* recenterSprite = CCSprite::createWithSpriteFrameName("GJ_undoBtn_001.png");
    CCMenuItemSpriteExtra* recenterBtn = CCMenuItemSpriteExtra::create(recenterSprite, this, menu_selector(ThumbnailPopup::recenter));

    recenterBtn->setPosition({5, 5});
    m_buttonMenu->addChild(recenterBtn);

    #ifdef GEODE_IS_MACOS
    recenterBtn->setVisible(false);
    #endif

    ButtonSprite* infoSprite = ButtonSprite::create(m_isPreview ? "Submit" : "What's this?");
    m_infoBtn = CCMenuItemSpriteExtra::create(infoSprite, this, menu_selector(ThumbnailPopup::openDiscordServerPopup));

    m_infoBtn->setPosition({m_mainLayer->getContentSize().width/2.f, 6});
    m_infoBtn->setVisible(m_isPreview);
    m_infoBtn->setZOrder(3);
    m_buttonMenu->addChild(m_infoBtn);

    m_theFunny = CCLabelBMFont::create(m_isPreview ? "Hiiii\ngeming\npopcorn\nskepper\nbob\nanvixo\nmoonstarmaster\ncdc\nlevel thumbnails bot\norangeyguy\ncrazytoast\nmeowy bonzai\nelliot" : "OwO", "bigFont.fnt");
    m_theFunny->setPosition(m_bgSprite->getPosition());
    m_theFunny->setVisible(m_isPreview);
    m_theFunny->setScale(0.25f);

    m_mainLayer->addChild(m_theFunny);

    m_loadingCircle->setParentLayer(m_mainLayer);
    m_loadingCircle->setPosition({m_mainLayer->getContentWidth()/2,m_mainLayer->getContentHeight()/2});
    m_loadingCircle->setAnchorPoint({0.5,0.5});
    m_loadingCircle->setScale(1.f);
    m_loadingCircle->ignoreAnchorPointForPosition(false);
    m_loadingCircle->show();


    if (!m_isPreview){
        m_downloadListener.spawn(
            ThumbnailManager::get().fetchThumbnail(m_levelID, ThumbnailManager::Quality::High),
            [this](Result<Ref<CCTexture2D>> result) {
                if (result.isOk()) {
                    this->onDownloadSuccess(result.unwrap());
                } else {
                    this->onDownloadError(result.unwrapErr());
                }
            }
        );

        this->loadThumbnailInfo();
    } else {
        CCTextureCache::get()->removeTextureForKey(this->m_previewFileName.c_str());
        this->onDownloadSuccess(Ref<CCTexture2D>(CCSprite::create(this->m_previewFileName.c_str())->getTexture()));

        auto editNoteBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_viewLevelsBtn_001.png"),
            this, menu_selector(ThumbnailPopup::onEditNote)
        );
        editNoteBtn->setPosition({m_mainLayer->getContentWidth(), 6});
        m_buttonMenu->addChild(editNoteBtn);

        m_downloadListener.spawn(
            ThumbnailManager::get().fetchThumbnail(m_levelID, ThumbnailManager::Quality::High),
            [this](Result<Ref<CCTexture2D>> result) {
                if (result.isOk()) {
                    this->onDownloadSuccess(result.unwrap());
                }
            }
        );
    }

    this->setMouseEnabled(true);

    return true;
}

void ThumbnailPopup::recenter(CCObject* sender) {
    if(CCNode* node = m_clippingNode->getChildByID("thumbnail")) {
        node->setPosition({m_mainLayer->getContentWidth()/2,m_mainLayer->getContentHeight()/2});
        node->stopAllActions();
        float scale = m_maxHeight/node->getContentSize().height;
        node->setUserObject("new-scale", CCFloat::create(scale));
        node->setScale(scale);
        node->setAnchorPoint({0.5,0.5});
    }
}

void ThumbnailPopup::enableSwapping() {
    if (!m_thumbnail || !m_swappedTexture) return;

    auto btn = Button::createWithSpriteFrameName("GJ_editModeBtn_001.png", [this](auto) {
        auto currentTexture = m_thumbnail->getTexture();
        currentTexture->retain();
        m_thumbnail->setTexture(m_swappedTexture);
        m_swappedTexture = currentTexture;
        currentTexture->release();

        m_usedSwap = !m_usedSwap;

        static_cast<CCLabelBMFont*>(m_mainLayer->getChildByID("viewing-mode-label"))
            ->setString(m_usedSwap ? "Viewing the original thumbnail" : "Viewing your screenshot");
    });

    btn->setScale(0.9f);
    btn->setPosition({m_mainLayer->getContentSize().width, m_mainLayer->getContentSize().height});
    btn->setID("swap-view-mode-btn");

    m_buttonMenu->addChild(btn);

    auto label = CCLabelBMFont::create("Viewing your screenshot", "goldFont.fnt");
    label->setScale(0.5f);
    label->setPosition({m_mainLayer->getContentSize().width / 2, -25.f});
    label->setID("viewing-mode-label");
    m_mainLayer->addChild(label);
}

void ThumbnailPopup::onDownloadSuccess(Ref<CCTexture2D> const& texture) {
    if (m_thumbnail) {
        m_swappedTexture = texture;
        m_isReplacement = true;
        this->enableSwapping();
        return;
    }

    // thanks for fucking this up sheepdotcom
    m_downloadBtn->setEnabled(true);
    m_downloadBtn->setColor({255,255,255});

    auto image = CCSprite::createWithTexture(texture);

    float scale = m_maxHeight/image->getContentSize().height;
    image->setScale(scale);
    image->setUserObject("scale", CCFloat::create(scale));
    image->setPosition({m_mainLayer->getContentWidth()/2, m_mainLayer->getContentHeight()/2});

    image->setID("thumbnail");
    m_thumbnail = image;

    m_clippingNode->addChild(image);
    m_loadingCircle->fadeAndRemove();
}

void ThumbnailPopup::onDownloadError(std::string const& error) {
    // thanks for the image cvolton ;)
    CCSprite* image = CCSprite::create("noThumb.png"_spr);
    float scale = m_maxHeight / image->getContentSize().height;
    image->setScale(scale);
    image->setUserObject("scale", CCFloat::create(scale));
    image->setPosition({m_mainLayer->getContentWidth()/2, m_mainLayer->getContentHeight()/2});
    image->setID("thumbnail");

    m_infoBtn->setVisible(true);
    m_theFunny->setVisible(true);
    m_clippingNode->addChild(image);
    m_loadingCircle->fadeAndRemove();
}

inline std::string toAgoString(int timestamp) {
    auto const fmtPlural = [](auto count, auto unit) {
        if (count == 1) {
            return fmt::format("{} {} ago", count, unit);
        }
        return fmt::format("{} {}s ago", count, unit);
    };
    auto const doSemiAccurate = [] (auto normal_ago, auto localtime) {
        // for customizability later on, right now it is just a passthrough
        return fmt::format("{}",normal_ago);
    };
    auto value = std::chrono::seconds(timestamp);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
    auto localtime = geode::localtime(timestamp);
    auto len = std::chrono::duration_cast<std::chrono::seconds>(now - value).count();
    if (len < 60) {
        return fmtPlural(len, "second");
    }
    len = std::chrono::duration_cast<std::chrono::minutes>(now - value).count();
    if (len < 60) {
        return fmtPlural(len, "minute");
    }
    len = std::chrono::duration_cast<std::chrono::hours>(now - value).count();
    if (len < 24) {
        return fmtPlural(len, "hour");
    }
    len = std::chrono::duration_cast<std::chrono::days>(now - value).count();
    if (len < 31) {
        return doSemiAccurate(fmtPlural(len, "day"),localtime);
    }
    len = std::chrono::duration_cast<std::chrono::weeks>(now - value).count();
    if (len < 4) {
        return doSemiAccurate(fmtPlural(len, "week"),localtime);
    }
    len = std::chrono::duration_cast<std::chrono::months>(now - value).count();
    if (len < 12) {
        return doSemiAccurate(fmtPlural(len, "month"),localtime);
    }
    len = std::chrono::duration_cast<std::chrono::years>(now - value).count();
    if (len >= 1) {
        return doSemiAccurate(fmtPlural(len, "year"),localtime);
    }
    return "";
}

// adapted from
// https://github.com/geode-sdk/geode/blob/2f390747385b2c7fcf15b606df10f87d671f3929/loader/src/server/Server.cpp#L262
static Result<uint64_t> parseISOTimestamp(std::string str) {
#ifdef GEODE_IS_WINDOWS
    std::stringstream ss(str);
    std::chrono::system_clock::time_point seconds;
    if (ss >> std::chrono::parse("%Y-%m-%dT%H:%M:%S", seconds)) {
        return Ok(std::chrono::duration_cast<std::chrono::seconds>(seconds.time_since_epoch()).count());
    }
    return Err("Invalid date time format '{}'", str);
#else
    auto dotPos = str.find('.');
    if (dotPos != std::string::npos) {
        str.resize(dotPos);
    }

    tm t;
    auto ptr = strptime(str.c_str(), "%Y-%m-%dT%H:%M:%S", &t);
    if (ptr == nullptr || (*ptr != '\0' && *ptr != 'Z')) {
        return Err("Invalid date time format '{}'", str);
    }

    return Ok(static_cast<uint64_t>(timegm(&t)));
#endif
}

static std::string readISOTimestamp(matjson::Value const& value) {
    auto res = value.asString();
    if (!res) return "Unknown";

    auto timeStr = std::move(res).unwrap();
    auto timeRes = parseISOTimestamp(timeStr);
    if (!timeRes) {
        //log::warn("{}", timeRes.unwrapErr());
        //return "Unknown";
        return timeStr;
    }

    auto time_int = timeRes.unwrap();
    auto tm = geode::localtime(time_int);
    auto ago_string = toAgoString(time_int);

    if (!ago_string.empty()) {
        if (Mod::get()->getSettingValue<bool>("always-show-accurate-stamps")) {
            return fmt::format("{} ({:%Y-%m-%d at %H:%M})", ago_string, tm);
        } else {
            return ago_string;
        }
    } else {
        return fmt::format("{:%Y-%m-%d at %H:%M}", tm);
    }
}

void ThumbnailPopup::loadThumbnailInfo() {
    SimpleTextArea* textArea = SimpleTextArea::create("Loading info...");
    textArea->setAnchorPoint({0, 0});
    textArea->setPosition({5, 5});
    textArea->setScale(0.7f);
    textArea->setVisible(false);

    LoadingSpinner* loader = LoadingSpinner::create(45.f/2);
    loader->setAnchorPoint({0, 0});
    loader->setPosition({2.5f, 2.5f});

    NineSlice* bg = NineSlice::create("square02b_001.png");
    bg->setColor({0, 0, 0});
    bg->setOpacity(120);
    bg->setScale(0.5f);
    //bg->setContentWidth(textArea->getScaledContentWidth()+10*2);
    //bg->setContentHeight(textArea->getScaledContentHeight()+10*2);
    bg->setContentWidth(55);
    bg->setContentHeight(55);
    bg->setAnchorPoint({0, 0});

    CCNode* container = CCNode::create();
    container->setID("thumbnail-info");
    container->setAnchorPoint({0, 0});
    container->setPosition({10, 10});
    m_buttonMenu->setZOrder(100);
    container->setZOrder(99);

    container->addChild(bg);
    container->addChild(textArea);
    container->addChild(loader);

    container->setVisible(Mod::get()->getSavedValue<bool>("show-info"));

    this->m_mainLayer->addChild(container);
    m_infoListener.spawn(
        web::WebRequest()
            .userAgent(USER_AGENT)
            .get(fmt::format("{}/thumbnail/{}/info", Settings::thumbnailAPIBaseURL(), m_levelID)),
        [textArea, container, bg, loader](web::WebResponse res) {
            if (!res.ok()) {
                container->setVisible(false);
                return;
            }

            auto json = res.json().unwrapOrDefault();

            auto uploader = json["username"].asString().unwrapOr("Unknown");
            auto accepter = json["accepted_by_username"].asString().unwrapOr("Unknown");

            auto upload_time = readISOTimestamp(json["upload_time"]);
            auto first_upload_time = readISOTimestamp(json["first_upload_time"]);
            auto accepted_time = readISOTimestamp(json["accepted_time"]);

            textArea->setVisible(true);
            textArea->setText(fmt::format(
                "Submitted by: {}\n"
                "Submitted: {}\n"
                "First submitted: {}\n"
                "Accepted by: {}\n"
                "Accepted: {}",
                uploader, upload_time,
                first_upload_time, accepter,
                accepted_time
            ));
            bg->setContentWidth((textArea->getScaledContentWidth()+10)*2);
            bg->setContentHeight((textArea->getScaledContentHeight()+10)*2);
            loader->removeFromParent();
        }
    );
}

ThumbnailPopup* ThumbnailPopup::create(int id, bool screenshotPreview) {
    auto ret = new ThumbnailPopup();
    ret->m_isPreview = false;
    ret->m_levelID = id;
    if (ret->init(-1)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

ThumbnailPopup* ThumbnailPopup::create(int id, std::string filename, std::string note) {
    auto ret = new ThumbnailPopup();
    ret->m_previewFileName = std::move(filename);
    ret->m_submissionNote = std::move(note);
    ret->m_isPreview = true;
    ret->m_levelID = id;
    if (ret->init(-1)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

float clip(float n, float lower, float upper) {
  return std::max(lower, std::min(n, upper));
}

bool ThumbnailPopup::ccTouchBegan(CCTouch* pTouch, CCEvent* event){
    if (m_touches.size() == 1 && m_thumbnail){
        //geode::log::info("this is where the second touch gets added");
        //thank you matcool
        auto firstTouch = *m_touches.begin();

        auto firstLoc = firstTouch->getLocation();
        auto secondLoc = pTouch->getLocation();

        this->m_touchMidPoint = (firstLoc + secondLoc) / 2.f;
        // save current zoom level
        this->m_initialScale = m_thumbnail->getScale();
        // distance between the two touches
        this->m_initialDistance = firstLoc.getDistance(secondLoc);
        // anchor point
        auto thumbnail = m_thumbnail;
        auto oldAnchor = thumbnail->getAnchorPoint();
        auto worldPos = thumbnail->convertToWorldSpace({0, 0});
        auto newAnchorX = (m_touchMidPoint.x-worldPos.x) / thumbnail->getScaledContentWidth();
        auto newAnchorY = (m_touchMidPoint.y-worldPos.y) / thumbnail->getScaledContentHeight();
        thumbnail->setAnchorPoint({clip(newAnchorX,0,1), clip(newAnchorY,0,1)});
        thumbnail->setPosition({
            thumbnail->getPositionX()+thumbnail->getScaledContentWidth()*-(oldAnchor.x-clip(newAnchorX,0,1)),
            thumbnail->getPositionY()+thumbnail->getScaledContentHeight()*-(oldAnchor.y-clip(newAnchorY,0,1))
        });
    }
    //geode::log::info("touch added");
    m_touches.insert(pTouch);
    return true;
}

void ThumbnailPopup::ccTouchMoved(CCTouch* pTouch, CCEvent* event){
    //geode::log::info("moved");
    if (m_touches.size() == 1){
        //geode::log::info("single touch");
        CCNode* thumbnail = m_thumbnail;
        if (!thumbnail) return;
        thumbnail->setPosition(thumbnail->getPosition() + pTouch->getDelta());
    }
    if (m_touches.size() == 2){
        this->wasZooming = true;
        //geode::log::info("double touch (EPIC!)");
        CCNode* thumbnail = m_thumbnail;
        if (!thumbnail) return;
        //thank you matcool
        auto it = m_touches.begin();
        auto firstTouch = *it;
        ++it;
        auto secondTouch = *it;

        auto firstLoc = firstTouch->getLocation();
        auto secondLoc = secondTouch->getLocation();
        auto center = (firstLoc + secondLoc) / 2;
        auto distNow = firstLoc.getDistance(secondLoc);

        auto const mult = this->m_initialDistance / distNow;
        auto zoom = clip(this->m_initialScale / mult, 0.2f, 6.5f);
        thumbnail->setScale(zoom);
        //geode::log::info("zoom {}",zoom);

        auto centerDiff = this->m_touchMidPoint - center;
        thumbnail->setPosition(thumbnail->getPosition() - centerDiff);
        this->m_touchMidPoint = center;
    }
}

void ThumbnailPopup::ccTouchEnded(CCTouch* pTouch, CCEvent* event){
    m_touches.erase(pTouch);
    if (wasZooming && m_touches.size() == 1 && m_thumbnail){
        auto thumbnail = m_thumbnail;
        auto scale = thumbnail->getScale();
        if (scale < 0.25f){
            thumbnail->runAction(
                CCEaseSineInOut::create(
                    CCScaleTo::create(0.5f, 0.25f)
                )
            );
        }
        if (scale > 4.0f){
            thumbnail->runAction(
                CCEaseSineInOut::create(
                    CCScaleTo::create(0.5f, 4.0f)
                )
            );
        }
        wasZooming = false;
    }
    //geode::log::info("ended");
}

void ThumbnailPopup::scrollWheel(float y, float x) {
    CCNode* thumbnail = m_thumbnail;
    if (!thumbnail) return;

    constexpr float zoomSpeed = 0.01f;

    float oldScale = thumbnail->getScale();
    float newScale = oldScale * std::pow(1.f + zoomSpeed, -y);
    newScale = clip(newScale, 0.25f, 6.f);

    if (std::abs(newScale - oldScale) < 0.0001f)
        return;

    CCPoint mouseWorld = getMousePos();
    CCPoint localBefore = thumbnail->convertToNodeSpace(mouseWorld);
    thumbnail->setScale(newScale);

    CCPoint worldAfter = thumbnail->convertToWorldSpace(localBefore);
    CCPoint diff = mouseWorld - worldAfter;
    thumbnail->setPosition(thumbnail->getPosition() + diff);
}
