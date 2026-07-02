#include <Geode/Geode.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/ui/Button.hpp>
#include <Geode/utils/web.hpp>

#include "../managers/AuthManager.hpp"
#include "../managers/SettingsManager.hpp"

using namespace geode::prelude;

class $modify(CommentCellHook, CommentCell) {
    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);

        if (m_accountComment || !comment) return;
        if (!Mod::get()->getSettingValue<bool>("thumb-role-badges")) return;

        if (auto role = AuthManager::get().getCachedBadgeForAccount(comment->m_accountID)) {
            this->addBadge(role.value(), m_compactMode);
        }
    }

    void addBadge(ThumbnailRole role, bool smallCell) {
        if (role == ThumbnailRole::NONE || role == ThumbnailRole::USER) return;

        auto& roleInfo = THUMBNAIL_ROLES[(int)role-1];

        auto badgeSprite = CCSprite::createWithSpriteFrameName(roleInfo.badge_sprite.data());
        badgeSprite->setScale(smallCell ? 0.55f : 0.75f);

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

        auto usernameMenu = m_mainLayer->getChildByIDRecursive("username-menu");
        if (!usernameMenu) return;

        usernameMenu->addChild(badgeButton);
        usernameMenu->updateLayout();
    }
};