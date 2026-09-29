#include "AuthManager.hpp"
#include "ThumbnailManager.hpp"

#include <Geode/Result.hpp>
#include <matjson.hpp>
#include <string>
#include <argon/argon.hpp>
#include <Geode/modify/AccountHelpLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

bool AuthManager::isLoggedIn() {
    return Mod::get()->hasSavedValue("token");
}

ThumbnailRole getRoleByName(std::string_view role) {
    auto it = THUMBNAIL_ROLE_SERVER_NAMES.find(role);
    if (it != THUMBNAIL_ROLE_SERVER_NAMES.end()) {
        return it->second;
    }
    return ThumbnailRole::NONE;
}

std::optional<ThumbnailRoleInfo> getRoleInfoByName(std::string_view role) {
    auto r = getRoleByName(role);
    if (r == ThumbnailRole::NONE) {
        return std::nullopt;
    }
    return THUMBNAIL_ROLES[static_cast<size_t>(r) - 1];
}

AuthManager& AuthManager::get() {
    static AuthManager instance;
    return instance;
}

static std::string getErrorMessage(web::WebResponse const& res) {
    if (auto jsonRes = res.json()) {
        auto json = std::move(jsonRes).unwrap();

        std::string message;
        if (json["reason"].isString() && res.code() != 403) {
            message = json["reason"].asString().unwrap();
        } else if (json["message"].isString()) {
            message = json["message"].asString().unwrap();
        }

        std::string details;
        if (json["details"].isString()) {
            details = json["details"].asString().unwrap();
        }

        if (!message.empty()) {
            if (!details.empty()) {
                return fmt::format("{}: <co>{}</c>", message, details);
            }
            return message;
        }
    }

    StringBuffer<> buf("HTTP ");
    buf.append("<cy>{}</c>", res.code());

    auto str = res.string().unwrapOrDefault();
    if (!str.empty()) {
        buf.append(": <cj>{}</c>", str);
    }

    if (!res.errorMessage().empty()) {
        buf.append(" (<cr>{}</c>)", res.errorMessage());
    }

    return buf.str();
}

AuthManager::UploadFuture AuthManager::uploadThumbnail(std::filesystem::path filename, int levelID, std::string note, Function<void(ZStringView)> onProgress) {
    if (onProgress) onProgress("Logging in...");

    if (!this->isLoggedIn()) {
        GEODE_CO_UNWRAP(co_await this->login());
    }

    if (m_serverMetadata.has_value()) {
        auto data = co_await async::waitForMainThread<argon::AccountData>([] {
            return argon::getGameAccountData();
        });

        if (!data->valid()) {
            co_return Err("Invalid account data");
        }

        if (data->serverUrl != m_serverMetadata->gdBaseUrl) {
            co_return Err("Incompatible Geometry Dash server URL: <cr>{}</c> (expected <cy>{}</c>)", data->serverUrl, m_serverMetadata->gdBaseUrl);
        }
    }

    if (onProgress) onProgress("Uploading...");

    GEODE_CO_UNWRAP_INTO(auto readRes, file::readBinary(filename));

    auto res = co_await web::WebRequest()
        .header("Authorization", fmt::format("Bearer {}", Mod::get()->getSavedValue<std::string>("token")))
        .header("X-Submission-Note", std::move(note))
        .body(std::move(readRes))
        .userAgent(USER_AGENT)
        .post(fmt::format("{}/upload/{}", Settings::thumbnailAPIBaseURL(), levelID));

    auto code = res.code();

    if (code == 201 || code == 200) {
        co_return Ok("The thumbnail has been <cg>applied</c>.");
    }

    if (code == 202) {
        co_return Ok("The thumbnail has been <co>submitted</c>, and is now <cj>in the queue</c> for approval.");
    }

    if (code == 401 || code == 498) {
        Mod::get()->getSaveContainer().erase("token");
    }

    auto err = getErrorMessage(res);

    switch (code) {
        case 400: co_return Err("<cr>{}</c>", err); // bad request: invalid image, broken notes
        case 423: { // locked
            if (auto reasonRes = res.json().unwrapOrDefault()["reason"].asString()) {
                co_return Err(
                    "Submissions are currently <cr>locked</c> for this level.\nReason: <cy>{}</c>",
                    reasonRes.unwrap()
                );
            }
            co_return Err("Submissions are currently <cr>locked</c> for this level.");
        }
        case 403: [[fallthrough]]; // forbidden: user banned
        case 426: [[fallthrough]]; // upgrade required: mod/gd outdated
        case 429: [[fallthrough]]; // too many requests: ran out of energy
        case 503: co_return Err(std::move(err)); // service unavailable: server down
        default: break;
    }

    log::error("Upload failed (HTTP {}): {}", code, res.string().unwrapOrDefault());
    co_return Err("Thumbnail upload <cr>failed</c>: {}", err);
}

AuthManager::LinkFuture AuthManager::linkAccount(std::string linkSecret) {
    if (!this->isLoggedIn()){
        auto loginRes = co_await this->login();
        if (!loginRes) {
            co_return Err(std::move(loginRes).unwrapErr());
        }
    }

    auto res = co_await web::WebRequest()
        .header("Authorization", fmt::format("Bearer {}", Mod::get()->getSavedValue<std::string>("token")))
        .bodyJSON(matjson::makeObject({{"token", std::move(linkSecret)}}))
        .userAgent(USER_AGENT)
        .post(fmt::format("{}/auth/link", Settings::thumbnailAPIBaseURL()));

    if (res.ok()) {
        Mod::get()->setSavedValue<std::string>(
            "token", res.json().unwrapOrDefault()["token"].asString().unwrapOrDefault()
        );
        co_return Ok("Your account was linked <cg>successfully</c>!");
    }

    auto err = getErrorMessage(res);
    log::error("Account link failed (HTTP {}): {}", res.code(), res.string().unwrapOrDefault());
    co_return Err(fmt::format("Account link <cr>failed</c>: {}", err));
}

void AuthManager::initialSync() {
    async::spawn(
        web::WebRequest()
            .userAgent(USER_AGENT)
            .get(fmt::format("{}/user/badges", Settings::thumbnailAPIBaseURL())),
        [this](web::WebResponse res) {
            if (!res.ok()) return;

            for (auto& [key, value] : res.json().unwrapOrDefault()["data"]) {
                auto role = getRoleByName(key);
                if (role == ThumbnailRole::NONE) continue;

                for (auto& accID : value) {
                    if (!accID.isNumber()) continue;
                    m_badgeCache[accID.asInt().unwrapOr(0)] = role;
                }
            }
        }
    );

    if (!this->isLoggedIn()) return;

    async::spawn(
        web::WebRequest()
            .header("Authorization", fmt::format("Bearer {}", Mod::get()->getSavedValue<std::string>("token")))
            .userAgent(USER_AGENT)
            .get(fmt::format("{}/auth/session", Settings::thumbnailAPIBaseURL())),
        [this](web::WebResponse res) {
            if (!res.ok()) {
                log::error("Session check failed: {}", getErrorMessage(res));
                m_myRole = getRoleByName(Mod::get()->getSavedValue<std::string>("cached_role"));
                return;
            }

            auto json = res.json().unwrapOrDefault();
            auto role = json["user"]["role"].asString().unwrapOr("user");
            m_myRole = getRoleByName(role);

            Mod::get()->setSavedValue<std::string>("cached_role", role);
        }
    );

    async::spawn(
        web::WebRequest()
            .userAgent(USER_AGENT)
            .get(fmt::format("{}/metadata", Settings::thumbnailAPIBaseURL())),
        [this](web::WebResponse res) {
            if (!res.ok()) {
                log::error("Metadata fetch failed: {}", getErrorMessage(res));
                return;
            }

            auto jsonRes = res.json();
            if (!jsonRes) return;

            auto json = std::move(jsonRes).unwrap();
            auto const& data = json["data"];

            m_serverMetadata = ServerMetadata{
                .gdBaseUrl = data["gd_base"].asString().unwrapOrDefault(),
                .argonBaseUrl = data["argon_host"].asString().unwrapOrDefault(),
                .bannedMods = data["banned_mods"].as<std::vector<std::string>>().unwrapOrDefault(),
                .bannedSettings = data["banned_settings"].as<StringMap<std::vector<std::string>>>().unwrapOrDefault()
            };
        }
    );
}

void AuthManager::purgeBadgeForAccount(int accountID) {
    m_badgeCache.erase(accountID);
}

std::optional<ThumbnailRole> AuthManager::getCachedBadgeForAccount(int accountID) {
    auto it = m_badgeCache.find(accountID);
    if (it != m_badgeCache.end()) {
        return it->second;
    }
    return std::nullopt;
}

AuthManager::BadgeFuture AuthManager::fetchBadgeForAccount(int accountID) {
    if (m_badgeCache.contains(accountID)) {
        co_return Ok(m_badgeCache[accountID]);
    }

    auto res = co_await web::WebRequest()
        .userAgent(USER_AGENT)
        .get(fmt::format("{}/user/gd/{}", Settings::thumbnailAPIBaseURL(), accountID));

    if (!res.ok()) {
        log::error("Badge fetch failed: {}", res.string().unwrapOrDefault());
        co_return Err(getErrorMessage(res));
    }

    auto role = res.json().unwrapOrDefault()["data"]["role"].asString().unwrapOrDefault();
    auto role_enum = getRoleByName(role);
    m_badgeCache[accountID] = role_enum;

    co_return Ok(role_enum);
}

Result<> AuthManager::validateModCompats() const {
    if (!m_serverMetadata.has_value()) {
        return Ok();
    }

    // check for banned mods
    {
        StringBuffer<> buf("Thumbnails <cr>can not be taken</c> due to the following mods being enabled:\n");

        bool hasBannedMods = false;
        for (auto const& modID : m_serverMetadata->bannedMods) {
            if (Loader::get()->isModLoaded(modID)) {
                buf.append("\n<mod:{}>\n", modID);
                hasBannedMods = true;
            }
        }

        if (hasBannedMods) {
            return Err(buf.str());
        }
    }

    // check for banned settings
    {
        StringBuffer<> buf("Thumbnails <cr>can not be taken</c> due to the following settings being enabled:\n");

        bool hasBannedSettings = false;
        for (auto const& [modID, settings] : m_serverMetadata->bannedSettings) {
            auto mod = Loader::get()->getLoadedMod(modID);
            if (!mod) continue;

            for (auto const& setting : settings) {
                if (mod->getSettingValue<bool>(setting)) {
                    auto settingInfo = mod->getSetting(setting);
                    if (!settingInfo) continue;
                    buf.append("- \"<co>{}</c>\" (<cj>{}</c>)", settingInfo->getDisplayName(), mod->getName());
                    hasBannedSettings = true;
                }
            }
        }

        if (hasBannedSettings) {
            return Err(buf.str());
        }
    }

    return Ok();
}

AuthManager::LoginFuture AuthManager::login() {
    if (GJAccountManager::get()->m_accountID == 0) {
        co_return Err("Not logged into Geometry Dash account!");
    }

    auto data = co_await async::waitForMainThread<argon::AccountData>([] {
        return argon::getGameAccountData();
    });

    if (!data->valid()) {
        co_return Err("Argon authentication failed: <cr>invalid game data</r>");
    }

    if (m_serverMetadata.has_value()) {
        if (data->serverUrl != m_serverMetadata->gdBaseUrl) {
            co_return Err("Incompatible Geometry Dash server URL: <cr>{}</c> (expected <cy>{}</c>)", data->serverUrl, m_serverMetadata->gdBaseUrl);
        }

        GEODE_CO_UNWRAP(argon::setServerUrl(m_serverMetadata->argonBaseUrl));
    }

    auto argonRes = co_await argon::startAuth(data.value());
    if (!argonRes) {
        co_return Err(fmt::format("Argon auth error: {}", argonRes.unwrapErr()));
    }

    auto accID = GJAccountManager::get()->m_accountID;
    auto userID = GameManager::get()->m_playerUserID.value();
    std::string username = GJAccountManager::get()->m_username;

    auto token = std::move(argonRes).unwrap();
    web::WebResponse res = co_await web::WebRequest()
        .bodyJSON(matjson::makeObject({
            {"account_id", accID},
            {"user_id", userID},
            {"username", std::move(username)},
            {"argon_token", std::move(token)}
        }))
        .userAgent(USER_AGENT)
        .post(fmt::format("{}/auth/login", Settings::thumbnailAPIBaseURL()));

    if (!res.ok()) {
        log::error("Login request failed: {}", res.string().unwrapOrDefault());
        co_return Err(getErrorMessage(res));
    }

    Mod::get()->setSavedValue<std::string>(
        "token", res.json().unwrapOrDefault()["token"].asString().unwrapOrDefault()
    );

    co_return Ok("Logged in <cg>successfully</c>!");
}

std::string AuthManager::getToken() {
    return Mod::get()->getSavedValue<std::string>("token");
}

class $modify(AccountHelpLayer) {
    void FLAlert_Clicked(FLAlertLayer* p0, bool p1) override {
        if (p0->getTag() == 4 && p1){
            Mod::get()->getSaveContainer().erase("token");
        }
        AccountHelpLayer::FLAlert_Clicked(p0, p1);
    }
};

$on_mod(Loaded) {
    listenForSettingChanges<std::string>("level-thumbnails-api", [](std::string value) {
        Mod::get()->getSaveContainer().erase("token");
    });

    AuthManager::get().initialSync();
}