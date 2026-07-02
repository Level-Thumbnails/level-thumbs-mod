#pragma once
#include <argon/argon.hpp>
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include "SettingsManager.hpp"
#include "../layers/LoadingOverlay.hpp"

enum class ThumbnailRole {
    NONE = 0,
    USER = 1,
    VERIFIED = 2,
    MODERATOR = 3,
    ADMIN = 4,
    OWNER = 5,
};

struct ThumbnailRoleInfo {
    std::string_view badge_sprite;
    std::string_view name;
    std::string_view description;

    constexpr ThumbnailRoleInfo(std::string_view badge_sprite, std::string_view name, std::string_view description)
        : badge_sprite(badge_sprite), name(name), description(description) {}
};

constexpr std::array THUMBNAIL_ROLES = {
    ThumbnailRoleInfo{
        "LT_Badge_THE.png"_spr,
        "User",
        "This user has submitted thumbnails to the <co>Level Thumbnails</c> mod."
    },
    ThumbnailRoleInfo{
        "LT_Badge_VerThumbnailer.png"_spr,
        "Verified Thumbnailer",
        "This user has submitted a lot of quality thumbnails to the <co>Level Thumbnails</c> mod."
    },
    ThumbnailRoleInfo{
        "LT_Badge_ThumbnailMod.png"_spr,
        "Thumbnail Moderator",
        "This user is authorized to review thumbnails for the <co>Level Thumbnails</c> mod."
    },
    ThumbnailRoleInfo{
        "LT_Badge_ThumbnailAdmin.png"_spr,
        "Thumbnail Admin",
        "This user is authorized to <cy>lock levels</c> and promote new <cj>Moderators</c> for the <co>Level Thumbnails</c> mod."
    },
    ThumbnailRoleInfo{
        "LT_Badge_Owner.png"_spr,
        "Owner",
        "This user is an owner of the <co>Level Thumbnails</c> mod."
    }
};

static geode::utils::StringMap<ThumbnailRole> THUMBNAIL_ROLE_SERVER_NAMES = {
    {"user", ThumbnailRole::USER},
    {"verified", ThumbnailRole::VERIFIED},
    {"moderator", ThumbnailRole::MODERATOR},
    {"admin", ThumbnailRole::ADMIN},
    {"owner", ThumbnailRole::OWNER}
};

inline ThumbnailRole getRoleByName(std::string_view role) {
    auto it = THUMBNAIL_ROLE_SERVER_NAMES.find(role);
    if (it != THUMBNAIL_ROLE_SERVER_NAMES.end()) {
        return it->second;
    }
    return ThumbnailRole::NONE;
}

inline std::optional<ThumbnailRoleInfo> getRoleInfoByName(std::string_view role) {
    auto r = getRoleByName(role);
    if (r == ThumbnailRole::NONE) {
        return std::nullopt;
    }
    return THUMBNAIL_ROLES[static_cast<size_t>(r) - 1];
}

class AuthManager {
private:
    AuthManager() = default;

public:
    AuthManager(AuthManager const&) = delete;
    AuthManager(AuthManager&&) = delete;
    AuthManager& operator=(AuthManager const&) = delete;
    AuthManager& operator=(AuthManager&&) = delete;

    using LoginResult = geode::Result<std::string>;
    using LinkResult = geode::Result<std::string>;
    using UploadResult = geode::Result<std::string>;
    using BadgeResult = geode::Result<ThumbnailRole>;

    using LoginFuture = arc::Future<LoginResult>;
    using LinkFuture = arc::Future<LinkResult>;
    using UploadFuture = arc::Future<UploadResult>;
    using BadgeFuture = arc::Future<BadgeResult>;

    static AuthManager& get();

    static bool isLoggedIn();
    static std::string getToken();

    LoginFuture login();
    UploadFuture uploadThumbnail(std::string_view filename, int levelID, std::string note, geode::Function<void(geode::ZStringView)> onProgress = nullptr);
    LinkFuture linkAccount(std::string linkSecret);

    void initialSync();
    void purgeBadgeForAccount(int accountID);
    std::optional<ThumbnailRole> getCachedBadgeForAccount(int accountID);
    BadgeFuture fetchBadgeForAccount(int accountID);
    std::optional<ThumbnailRole> myRole() const { return m_myRole; }
    bool roleIsEqualOrAbove(ThumbnailRole role) const {
        auto myRole = this->myRole();
        return myRole.has_value() && myRole.value() >= role;
    }

private:
    std::unordered_map<int, ThumbnailRole> m_badgeCache = {};
    std::optional<ThumbnailRole> m_myRole = std::nullopt;
};