#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/ui/Button.hpp>
#include <Geode/utils/web.hpp>

#include "../managers/AuthManager.hpp"
#include "../managers/SettingsManager.hpp"

using namespace geode::prelude;

#include <alphalaneous.badgify/include/Badgify.hpp>

// most of this source file is unused, see the $on_mod(Loaded) macro further down for badges
/*
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
*/

$on_mod(Loaded) {
    alpha::badgify::registerBadge(
        "user"_spr,
        "User",
        "This user has submitted thumbnails to the <co>Level Thumbnails</c> mod.",
        [] (const alpha::badgify::Badge& badge) {
            if (!Mod::get()->getSettingValue<bool>("thumb-role-badges")) return;

            ThumbnailRole role = AuthManager::get().getCachedBadgeForAccount(badge.user.data()->m_accountID);
            if (!role.has_value()) {
                async::spawn(
                    AuthManager::get().fetchBadgeForAccount(badge.user.data()->m_accountID),
                    [badge](Result<ThumbnailRole> res) {
                        if (!res || res.unwrap() == ThumbnailRole::NONE || res.unwrap() != ThumbnailRole::USER) return;
                        CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_THE.png"_spr);
                        Loader::get()->queueInMainThread([badge, spr = geode::Ref(theBadgeSprite)]() {
                            alpha::badgify::showBadge(badge, spr);
                        });
                    }
                );
                return;
            }

            if (role.value() == ThumbnailRole::NONE || role.value() != ThumbnailRole::USER) return;

            CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_THE.png"_spr);
            alpha::badgify::showBadge(badge, theBadgeSprite);
        }
    );
    alpha::badgify::registerBadge(
        "verified-thumbnailer"_spr,
        "Verified Thumbnailer",
        "This user has submitted a lot of quality thumbnails to the <co>Level Thumbnails</c> mod.",
        [] (const alpha::badgify::Badge& badge) {
            if (!Mod::get()->getSettingValue<bool>("thumb-role-badges")) return;

            ThumbnailRole role = AuthManager::get().getCachedBadgeForAccount(badge.user.data()->m_accountID);
            if (!role.has_value()) {
                async::spawn(
                    AuthManager::get().fetchBadgeForAccount(badge.user.data()->m_accountID),
                    [badge](Result<ThumbnailRole> res) {
                        if (!res || res.unwrap() == ThumbnailRole::NONE || res.unwrap() != ThumbnailRole::VERIFIED) return;
                        CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_VerThumbnailer.png"_spr);
                        Loader::get()->queueInMainThread([badge, spr = geode::Ref(theBadgeSprite)]() {
                            alpha::badgify::showBadge(badge, spr);
                        });
                    }
                );
                return;
            }

            if (role.value() == ThumbnailRole::NONE || role.value() != ThumbnailRole::VERIFIED) return;

            CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_VerThumbnailer.png"_spr);
            alpha::badgify::showBadge(badge, theBadgeSprite);
        }
    );
    alpha::badgify::registerBadge(
        "thumbnail-moderator"_spr,
        "Thumbnail Moderator",
        "This user is authorized to review thumbnails for the <co>Level Thumbnails</c> mod.",
        [] (const alpha::badgify::Badge& badge) {
            if (!Mod::get()->getSettingValue<bool>("thumb-role-badges")) return;

            ThumbnailRole role = AuthManager::get().getCachedBadgeForAccount(badge.user.data()->m_accountID);
            if (!role.has_value()) {
                async::spawn(
                    AuthManager::get().fetchBadgeForAccount(badge.user.data()->m_accountID),
                    [badge](Result<ThumbnailRole> res) {
                        if (!res || res.unwrap() == ThumbnailRole::NONE || res.unwrap() != ThumbnailRole::MODERATOR) return;
                        CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_ThumbnailMod.png"_spr);
                        Loader::get()->queueInMainThread([badge, spr = geode::Ref(theBadgeSprite)]() {
                            alpha::badgify::showBadge(badge, spr);
                        });
                    }
                );
                return;
            }

            if (role.value() == ThumbnailRole::NONE || role.value() != ThumbnailRole::MODERATOR) return;

            CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_ThumbnailMod.png"_spr);
            alpha::badgify::showBadge(badge, theBadgeSprite);
        }
    );
    alpha::badgify::registerBadge(
        "thumbnail-admin"_spr,
        "Thumbnail Admin",
        "This user is authorized to <cy>lock level thumbnails</c> and promote new <cj>Moderators</c> for the <co>Level Thumbnails</c> mod.",
        [] (const alpha::badgify::Badge& badge) {
            if (!Mod::get()->getSettingValue<bool>("thumb-role-badges")) return;

            ThumbnailRole role = AuthManager::get().getCachedBadgeForAccount(badge.user.data()->m_accountID);
            if (!role.has_value()) {
                async::spawn(
                    AuthManager::get().fetchBadgeForAccount(badge.user.data()->m_accountID),
                    [badge](Result<ThumbnailRole> res) {
                        if (!res || res.unwrap() == ThumbnailRole::NONE || res.unwrap() != ThumbnailRole::ADMIN) return;
                        CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_ThumbnailAdmin.png"_spr);
                        Loader::get()->queueInMainThread([badge, spr = geode::Ref(theBadgeSprite)]() {
                            alpha::badgify::showBadge(badge, spr);
                        });
                    }
                );
                return;
            }

            if (role.value() == ThumbnailRole::NONE || role.value() != ThumbnailRole::ADMIN) return;

            CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_ThumbnailAdmin.png"_spr);
            alpha::badgify::showBadge(badge, theBadgeSprite);
        }
    );
    alpha::badgify::registerBadge(
        "owner"_spr,
        "Owner",
        "This user is an owner of the <co>Level Thumbnails</c> mod.",
        [] (const alpha::badgify::Badge& badge) {
            if (!Mod::get()->getSettingValue<bool>("thumb-role-badges")) return;

            ThumbnailRole role = AuthManager::get().getCachedBadgeForAccount(badge.user.data()->m_accountID);
            if (!role.has_value()) {
                async::spawn(
                    AuthManager::get().fetchBadgeForAccount(badge.user.data()->m_accountID),
                    [badge](Result<ThumbnailRole> res) {
                        if (!res || res.unwrap() == ThumbnailRole::NONE || res.unwrap() != ThumbnailRole::OWNER) return;
                        CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_Owner.png"_spr);
                        Loader::get()->queueInMainThread([badge, spr = geode::Ref(theBadgeSprite)]() {
                            alpha::badgify::showBadge(badge, spr);
                        });
                    }
                );
                return;
            }

            if (role.value() == ThumbnailRole::NONE || role.value() != ThumbnailRole::OWNER) return;

            CCSprite* theBadgeSprite = CCSprite::createWithSpriteFrameName("LT_Badge_Owner.png"_spr);
            alpha::badgify::showBadge(badge, theBadgeSprite);
        }
    );
}