#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include "../layers/ThumbnailPopup.hpp"
#include "../managers/SettingsManager.hpp"
#include "../utils/EclipseCompat.hpp"
#include "../utils/ModNodeCompat.hpp"
#include "../utils/NodeHider.hpp"
#include "../utils/RenderTexture.hpp"
#include "../utils/SubmissionNotes.hpp"

#include <prevter.imageplus/api.hpp>

using namespace geode::prelude;

class $modify(LTPlayLayer, PlayLayer) {
    struct Fields {
        TaskHolder<web::WebResponse> isPendingCheck;
        std::optional<uint32_t> lastDeathTick;
        uint32_t pendingCount = 0;
    };

    static void onModify(auto& self) {
        (void) self.setHookPriority("PlayLayer::destroyPlayer", -0x600000);
    }

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) {
            return false;
        }

        if (!Settings::thumbnailTakingEnabled()) {
            return true;
        }

        if (!level || level->m_levelID <= 0 || level->m_levelType == GJLevelType::Editor) {
            return true;
        }

        m_fields->isPendingCheck.spawn(
            web::WebRequest()
                .userAgent(USER_AGENT)
                .get(fmt::format("{}/pending/level/{}", Settings::thumbnailAPIBaseURL(), level->m_levelID)),
            [this](web::WebResponse res) {
                if (!res.ok()) {
                    log::error("Pending check failed: {}", res.errorMessage());
                    return;
                }

                auto json = res.json().unwrapOrDefault();
                m_fields->pendingCount = json["count"].asInt().unwrapOr(0);
            }
        );

        return true;
    }

    bool isCurrentlyDead() {
        if (m_playerDied || m_player1->m_isDead) return true;

        auto& fields = *m_fields.self();
        if (fields.lastDeathTick.has_value()) {
            constexpr uint32_t DEATH_TICK_TIMEOUT = 10; // ~20ms window
            if (m_gameState.m_currentProgress - fields.lastDeathTick.value() <= DEATH_TICK_TIMEOUT) {
                return true;
            }

            fields.lastDeathTick.reset();
        }

        return false;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) override {
        if (object != m_anticheatSpike) {
            m_fields->lastDeathTick = m_gameState.m_currentProgress;
        } else {
            m_fields->lastDeathTick.reset();
        }
        PlayLayer::destroyPlayer(player, object);
    }
};

static Hook* s_visitHook = nullptr;
static FunctionRef<void()> s_visitCallback = []{};

class $modify(LTBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        s_visitHook = self.getHook("GJBaseGameLayer::visitWithColorFlash").unwrap();
        s_visitHook->setAutoEnable(false);
    }

    $override void visitWithColorFlash() { s_visitCallback(); }

    static void runCustomVisit(GJBaseGameLayer* self, FunctionRef<void()> callback) {
        s_visitCallback = callback;

        bool flashVisibleOrig = self->m_flashNode->isVisible();
        self->m_flashNode->setVisible(true);

        (void) s_visitHook->enable();
        self->visit();
        (void) s_visitHook->disable();

        self->m_flashNode->setVisible(flashVisibleOrig);
    }
};

static bool sizesMatch(CCSize const& a, CCSize const& b) {
    constexpr float EPSILON = 0.1f;
    return std::abs(a.width - b.width) < EPSILON && std::abs(a.height - b.height) < EPSILON;
}

class $modify(ThumbnailPauseLayer, PauseLayer) {
    struct Fields {
        bool m_shownCloseToStartWarning = false;
        bool m_shownLowDetailWarning = false;
        bool m_shownAlreadyHasPending = false;
    };

    void customSetup() {
        PauseLayer::customSetup();

        if (!Settings::thumbnailTakingEnabled()) {
            return;
        }

        auto playLayer = PlayLayer::get();
        if (!playLayer || !playLayer->m_level || playLayer->m_level->m_levelID <= 0 || playLayer->m_level->m_levelType == GJLevelType::Main) {
            return;
        }

        auto rightButtonMenu = this->getChildByIDRecursive("right-button-menu");
        if (!rightButtonMenu) {
            return;
        }

        auto screenshotSprite = CCSprite::createWithSpriteFrameName("thumbnailButton.png"_spr);
        screenshotSprite->setScale(0.65f);

        if (static_cast<LTPlayLayer*>(playLayer)->m_fields->pendingCount > 0) {
            auto alreadyPending = CCSprite::createWithSpriteFrameName("highObjectIcon_001.png");
            alreadyPending->setPosition({ 40.f, 40.f });
            alreadyPending->setScale(1.5f);
            screenshotSprite->addChild(alreadyPending);
        }

        auto screenshotButton = CCMenuItemSpriteExtra::create(screenshotSprite, this, menu_selector(ThumbnailPauseLayer::onScreenshot));
        screenshotButton->setID("take-screenshot"_spr);

        rightButtonMenu->addChild(screenshotButton);
        rightButtonMenu->updateLayout();
    }

    void onScreenshot(CCObject*) {
    #ifdef GEODE_IS_MOBILE
        if (!Loader::get()->getLoadedMod("weebify.high-graphics-android")) {
            MDPopup::create(
                "Screenshot Error",
                "Thumbnails can only be taken with <cy>High Graphics</c> quality enabled.\n"
                "Please install the \"<cl>High Graphics on Mobile</c>\" mod and enable the change in Geometry Dash settings:\n\n"
                "<mod:weebify.high-graphics-android>\n\n"
                "Additionally make sure to install the \"<cl>Shader Precision Fix</c>\" mod:\n\n"
                "<mod:prevter.shader-precision-fix>",
                "OK"
            )->show();
            return;
        }

        if (!Loader::get()->getLoadedMod("prevter.shader-precision-fix")) {
            MDPopup::create(
                "Screenshot Error",
                "Thumbnails can only be taken with \"<cl>Shader Precision Fix</c>\" mod installed.\n"
                "Please install the mod and try again:\n\n"
                "<mod:prevter.shader-precision-fix>",
                "OK"
            )->show();
            return;
        }
    #endif

        if (CCDirector::get()->getContentScaleFactor() < 4.f) {
            FLAlertLayer::create(
                "Screenshot Error",
                "Thumbnails can only be taken with <cy>High Graphics</c> quality enabled.\n"
                "Please enable it in the Geometry Dash settings and try again.",
                "OK"
            )->show();
            return;
        }

        if (GameManager::get()->m_performanceMode) {
            FLAlertLayer::create(
                "Screenshot Error",
                "Thumbnails cannot be taken while <cy>Low Detail Mode</c> is enabled.\n"
                "Please disable it (found in Settings > Help) and try again.",
                "OK"
            )->show();
            return;
        }

        if (GameManager::get()->getGameVariable(GameVar::ExtraLDM)) {
            FLAlertLayer::create(
                "Screenshot Error",
                "Thumbnails cannot be taken while <cy>Extra LDM</c> is enabled.\n"
                "Please disable it (found in Settings > Options > Performance) and try again.",
                "OK"
            )->show();
            return;
        }

        auto compats = AuthManager::get().validateModCompats();
        if (!compats) {
            MDPopup::create(
                "Screenshot Error",
                std::move(compats).unwrapErr(),
                "OK"
            )->show();
            return;
        }

        auto playLayer = PlayLayer::get();
        if (!playLayer) {
            return;
        }

        auto eclipse = compat::getEclipse();
        if (eclipse.isErr()) {
            FLAlertLayer::create(
                "Screenshot Error",
                std::move(eclipse).unwrapErr(),
                "OK"
            )->show();
            return;
        }

        auto pendingCount = static_cast<LTPlayLayer*>(playLayer)->m_fields->pendingCount;
        if (pendingCount > 0) {
            if (!m_fields->m_shownAlreadyHasPending) {
                m_fields->m_shownAlreadyHasPending = true;
                FLAlertLayer::create(
                    "Warning",
                    fmt::format(
                        "This level already has <cy>{}</c> pending thumbnail(s).\n\n"
                        "Your submission may be <cr>rejected</c> to avoid duplicates. "
                        "Please consider waiting until the current queue is cleared and request a replacement if needed.",
                        pendingCount
                    ),
                    "OK"
                )->show();
                return;
            }
        }

        if (static_cast<LTPlayLayer*>(playLayer)->isCurrentlyDead()) {
            FLAlertLayer::create(
                "Screenshot Error",
                "You cannot take a thumbnail while the player is <cr>dead</c>.\n"
                "Please wait until you respawn and try again.",
                "OK"
            )->show();
            return;
        }

        if (!AuthManager::get().roleIsEqualOrAbove(ThumbnailRole::VERIFIED)) {
            constexpr double MIN_TIME = 5.0;
            if (playLayer->m_gameState.m_levelTime < MIN_TIME && !m_fields->m_shownCloseToStartWarning) {
                m_fields->m_shownCloseToStartWarning = true;
                FLAlertLayer::create(
                    "Warning",
                    "You are trying to take a screenshot <cy>very close to level start</c>!\n"
                    "There's a high chance it will be <cr>rejected</c> by moderators, "
                    "only proceed if you're sure there are no better places to take the screenshot.",
                    "OK"
                )->show();
                return;
            }

            if (playLayer->m_lowDetailMode && !m_fields->m_shownLowDetailWarning) {
                m_fields->m_shownLowDetailWarning = true;
                FLAlertLayer::create(
                    "Warning",
                    "You are trying to take a screenshot with <cy>Low Detail Mode</c> enabled for this level!\n"
                    "In most cases this will lead to <cr>rejection</c> unless the level requires it as a gimmick.\n"
                    "Consider disabling it and re-entering the level.",
                    "OK"
                )->show();
                return;
            }
        }

        std::vector<globed::ProgressArrow*> progressArrows;
        for (auto child : playLayer->getChildrenExt()) {
            if (auto arrow = typeinfo_cast<globed::ProgressArrow*>(child)) {
                progressArrows.push_back(arrow);
                arrow->setScale(0.f);
            }
        }

        // hide UI stuff
        HIDE_NODE2(playLayer->m_uiLayer);
        HIDE_NODE2(playLayer->m_percentageLabel);
        HIDE_NODE2(playLayer->m_progressBar);
        HIDE_NODE2(playLayer->m_attemptLabel);
        HIDE_NODE2(playLayer->m_debugDrawNode);
        HIDE_NODE2(playLayer->m_infoLabel);

        // hide respawn circles
        std::vector<HideCircleWave> hideCircleNodes;
        for (auto circle : playLayer->m_circleWaveArray->asExt<CCCircleWave*>()) {
            if (circle->m_target == playLayer->m_player1 || circle->m_target == playLayer->m_player2) {
                hideCircleNodes.emplace_back(circle);
            }
        }

        // hide practice checkpoints
        std::vector<HideGameObject> hideGameObjects;
        if (playLayer->m_isPracticeMode) {
            if (auto current = playLayer->getLastCheckpoint()) {
                if (auto obj = current->m_physicalCheckpointObject) {
                    hideGameObjects.emplace_back(obj);
                }
            }

            for (auto checkpoint : playLayer->m_checkpointArray->asExt<CheckpointObject*>()) {
                if (checkpoint == playLayer->getLastCheckpoint()) continue;
                if (auto obj = checkpoint->m_physicalCheckpointObject) {
                    hideGameObjects.emplace_back(obj);
                }
            }
        }

        // explode player particles
        std::vector<HideNode> hideNodes;
        for (auto obj : playLayer->m_objectLayer->getChildrenExt()) {
            if (typeinfo_cast<ExplodeItemNode*>(obj)) {
                hideNodes.emplace_back(obj);
            }
            if (obj->getTag() == 234562345) {
                hideNodes.emplace_back(obj);
            }
        }

        // mods
        HIDE_NODE(playLayer, "mat.run-info/RunInfoWidget");
        HIDE_NODE(playLayer, "cheeseworks.speedruntimer/timer");
        HIDE_NODE(playLayer, "sawblade.dim_mode/opacityLabel");
        HIDE_NODE(playLayer, "zilko.xdbot/state-label");
        HIDE_NODE(playLayer, "zilko.xdbot/frame-label");
        HIDE_NODE(playLayer, "zilko.xdbot/recording-audio-label");
        HIDE_NODE(playLayer, "zilko.xdbot/button-menu");
        HIDE_NODE(playLayer, "dankmeme.globed2/game-overlay");
        HIDE_NODE(playLayer, "dankmeme.globed2/player-node");
        HIDE_NODE(playLayer, "tobyadd.gdh/labels_top_left");
        HIDE_NODE(playLayer, "tobyadd.gdh/labels_top_right");
        HIDE_NODE(playLayer, "tobyadd.gdh/labels_bottom_left");
        HIDE_NODE(playLayer, "tobyadd.gdh/labels_bottom_right");
        HIDE_NODE(playLayer, "tobyadd.gdh/labels_bottom");
        HIDE_NODE(playLayer, "tobyadd.gdh/labels_top");
        HIDE_NODE(playLayer, "thesillydoggo.qolmod/noclip-tint-overlay");
        HIDE_NODE(playLayer, "zilko.editor_trail_in_game/drawy-node");
        HIDE_NODE(playLayer, "kevadroz.practicecheckpointpermanence/permanent-checkpoints");
        auto aboveShaderNode = playLayer->m_shaderLayer->getParent();
        HIDE_NODE(aboveShaderNode, "eclipse.eclipse-menu/hitboxes");
        HIDE_NODE(aboveShaderNode, "eclipse.eclipse-menu/show-trajectory-draw-node");

        // megahack & qolmod imo
        HIDE_NODE2(playLayer->getChildByType<core::Poller>(0));
        HIDE_NODE2(playLayer->getChildByType<status::Manager>(0));
        HIDE_NODE2(playLayer->getChildByType<ShowTrajectory>(0));
        HIDE_NODE2(playLayer->getChildByType<NoclipTint>(0));
        HIDE_NODE2(playLayer->getChildByType<HitboxNode>(0));
        HIDE_NODE2(playLayer->getChildByType<qolmod::TrajectoryNode>(0));
        HIDE_NODE2(playLayer->getChildByType<qolmod::CoinTracerNode>(0));

        auto oldScale = playLayer->getScaleY();
        playLayer->setScaleY(-oldScale); // flip y-axis because opengl

        auto shader = playLayer->m_shaderLayer;
        bool hadShader = shader->getParent() != nullptr && shader->m_targetTextureSize != CCSize{1920, 1080};
        CCSize oldScreenSize{};
        CCSize oldShaderTargetTextureSize = shader->m_targetTextureSize;

        std::unique_ptr<uint8_t[]> data;
        std::optional<CCPoint> oldUILayerPos = std::nullopt;
        bool aspectMatches = false;
        bool pixelateHardEdges = false;
        {
            auto oldWinSize = CCDirector::get()->getWinSize();

            RenderTexture rt(1920, 1080);
            rt.begin();

            auto newWinSize = CCDirector::get()->getWinSize();
            aspectMatches = sizesMatch(oldWinSize, newWinSize);

            static constexpr auto MIN_DELTA = std::numeric_limits<float>::min();

            if (!aspectMatches) {
                playLayer->setContentSize(newWinSize);

                // ground fixes
                playLayer->m_calculateTargetHeightOffset = true;
                playLayer->m_updateGroundShadows = true;
                playLayer->m_groundLayer->updateGroundWidth(true);
                playLayer->m_groundLayer2->updateGroundWidth(true);
                auto g01 = playLayer->m_effectManager->activeColorForIndex(1001);
                playLayer->m_groundLayer->updateGround01Color(g01);
                playLayer->m_groundLayer2->updateGround01Color(g01);
                if (playLayer->m_groundLayer->m_ground2Sprite) {
                    auto g02 = playLayer->m_effectManager->activeColorForIndex(1009);
                    playLayer->m_groundLayer2->updateGround02Color(g02);
                    playLayer->m_groundLayer2->updateGround02Color(g02);
                }

                playLayer->updateCamera(MIN_DELTA);

                // ui trigger layer
                if (auto uiTriggerLayer = playLayer->m_uiTriggerUI) {
                    if (uiTriggerLayer->getChildrenCount() > 0) {
                        auto delta = newWinSize - oldWinSize;
                        oldUILayerPos = uiTriggerLayer->getPosition();
                        uiTriggerLayer->setPosition(uiTriggerLayer->getPosition() + delta);
                    }
                }
            }

            if (hadShader) {
                // fix shaderlayer
                pixelateHardEdges = shader->m_state.m_pixelateHardEdges;
                oldScreenSize = shader->m_screenSize;
                shader->m_screenSize = newWinSize;
                shader->m_targetTextureSize = CCSize{1920, 1080};
                shader->setupShader(false);
                if (!pixelateHardEdges) {
                    ccTexParams a = {GL_LINEAR,GL_LINEAR};
                    shader->m_sprite->getTexture()->setTexParameters(&a);
                }
                shader->prePixelateShader();
                playLayer->updateShaderLayer(MIN_DELTA);
            }

            if (!aspectMatches) {
                auto& areaEffects = playLayer->m_gameState.m_unsortedAreaEffects;
                areaEffects.insert(static_cast<int>(GJAreaActionType::Fade));
                areaEffects.insert(static_cast<int>(GJAreaActionType::Tint));

                playLayer->m_gameState.m_commandIndex++;
                playLayer->updateVisibility(0.f);
                playLayer->m_gameState.m_commandIndex--;
            }

            manualVisit(playLayer);

            if (!aspectMatches) {
                // restore ground
                playLayer->m_updateGroundShadows = true;
                playLayer->m_calculateTargetHeightOffset = true;
                playLayer->m_groundLayer->updateGroundWidth(true);
                playLayer->m_groundLayer2->updateGroundWidth(true);

                playLayer->setContentSize(newWinSize);
            }

            data = rt.getData();
            rt.end();
        }

        playLayer->setScaleY(oldScale);
        if (hadShader) {
            shader->m_screenSize = oldScreenSize;
            shader->m_targetTextureSize = oldShaderTargetTextureSize;
            shader->setupShader(false);
            if (!pixelateHardEdges) {
                ccTexParams a = {GL_LINEAR, GL_LINEAR};
                shader->m_sprite->getTexture()->setTexParameters(&a);
            }
            shader->prePixelateShader();
        }

        if (!aspectMatches && oldUILayerPos) {
            playLayer->m_uiTriggerUI->setPosition(*oldUILayerPos);
        }

        for (auto arrow : progressArrows) {
            arrow->setScale(1.f);
        }

        if (!data) {
            log::error("Failed to take screenshot: could not get pixel data");
            return;
        }

        auto res = imgp::encode::webp(data.get(), 1920, 1080, true, 100);
        if (!res) {
            log::error("Failed to take screenshot: {}", res.unwrapErr());
            return;
        }

        auto levelID = playLayer->m_level->m_levelID;
        auto saveDir = Mod::get()->getSaveDir() / fmt::format("{}.webp", levelID);
        if (auto saveRes = file::writeBinary(saveDir, *res); !saveRes) {
            log::error("Failed to save screenshot: {}", saveRes.unwrapErr());
            return;
        }

        ThumbnailPopup::create(levelID, saveDir.string(), notes::buildSubmissionNote())->show();
    }

    static void GLScalef(float x, float y, float z) {
    #ifdef GEODE_IS_IOS
        kmMat4 mat{};
        mat.mat[0] = x;
        mat.mat[5] = y;
        mat.mat[10] = z;
        mat.mat[15] = 1.f;
        kmGLMultMatrix(&mat);
    #else
        kmGLScalef(x, y, z);
    #endif
    }

    // idea by undefined06855 from rewind mod
    // original: https://github.com/undefined06855/Rewind/blob/0281e11b2c1c35c878786c5a6ce46c3961a71214/src/hooks/GJBaseGameLayer.cpp#L150
    static void manualVisit(PlayLayer* playLayer) {
        std::array<CCNode*, 8> nodes = {
            playLayer->m_objectParent,
            playLayer->m_inShaderParent,
            playLayer->m_shaderLayer,
            playLayer->m_aboveShaderParent,
            playLayer->m_objectLayer,
            playLayer->m_inShaderObjectLayer,
            playLayer->m_aboveShaderObjectLayer,
            playLayer->m_uiTriggerUI
        };

        std::ranges::sort(nodes, [](CCNode* left, CCNode* right) {
            if (!left || !right) return left != nullptr;
            return left->getZOrder() < right->getZOrder();
        });

        auto winSize = CCDirector::get()->getWinSize();

        kmGLPushMatrix();
        kmGLTranslatef(0.f, winSize.height, 0.f);
        GLScalef(1.f, -1.f, 1.f); // flip y-axis because opengl

        LTBaseGameLayer::runCustomVisit(playLayer, [&] {
            for (auto* node : nodes) {
                if (!node || node->getParent() != playLayer) continue;
                node->visit();
            }
        });

        kmGLPopMatrix();
    }
};
