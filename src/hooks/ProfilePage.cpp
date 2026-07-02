#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/ui/Button.hpp>
#include <Geode/utils/web.hpp>

#include "../managers/AuthManager.hpp"
#include "../managers/SettingsManager.hpp"

using namespace geode::prelude;

class $modify(ProfilePageHook,ProfilePage) {
    struct Fields {
        TaskHolder<AuthManager::BadgeResult> m_userInfoListener;
    };

    void addBadge(ThumbnailRole role) {
        if (role == ThumbnailRole::NONE || role == ThumbnailRole::USER) return;

        auto existingBadge = this->getChildByIDRecursive("levelthumbs-badge"_spr);
        if (existingBadge) existingBadge->removeFromParent();

        auto& roleInfo = THUMBNAIL_ROLES[(int)role-1];

        auto badgeSprite = CCSprite::createWithSpriteFrameName(roleInfo.badge_sprite.data());
        // badgeSprite->setAnchorPoint({0.5f,0.55f});
        auto badgeButton = Button::createWithNode(badgeSprite, [roleInfo](auto sender) {
            createQuickPopup(
                roleInfo.name.data(),
                std::string(roleInfo.description),
                "OK",
                nullptr,
                [](auto, bool) {},
                true
            );
        });

        badgeButton->setID("levelthumbs-badge"_spr);
        
        auto usernameMenu = this->getChildByIDRecursive("username-menu");
        usernameMenu->addChild(badgeButton);
        usernameMenu->updateLayout();
    }

    void onUpdate(CCObject* sender) {
        ProfilePage::onUpdate(sender);

        if (!Mod::get()->getSettingValue<bool>("thumb-role-badges")) return;

        auto& AM = AuthManager::get();

        AM.purgeBadgeForAccount(m_accountID);
        m_fields->m_userInfoListener.spawn(
            AM.fetchBadgeForAccount(m_accountID),
            [this](Result<ThumbnailRole> res) {
                if (res) {
                    this->addBadge(res.unwrap());
                }
            }
        );
    }

    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);

        if (!Mod::get()->getSettingValue<bool>("thumb-role-badges")) return;

        if (auto role = AuthManager::get().getCachedBadgeForAccount(m_accountID)) {
            this->addBadge(role.value());
        }
    }
};