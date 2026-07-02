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

AuthManager& AuthManager::get() {
    static AuthManager instance;
    return instance;
}

AuthManager::UploadFuture AuthManager::uploadThumbnail(std::string_view filename, int levelID, std::string note, Function<void(ZStringView)> onProgress) {
    if (onProgress) onProgress("Logging in...");

    if (!this->isLoggedIn()) {
        auto loginRes = co_await this->login();
        if (!loginRes) {
            co_return Err(std::move(loginRes).unwrapErr());
        }
    }

    if (onProgress) onProgress("Uploading...");

    auto readRes = file::readBinary(filename);
    if (!readRes) {
        co_return Err(std::move(readRes).unwrapErr());
    }

    auto res = co_await web::WebRequest()
        .header("Authorization", fmt::format("Bearer {}", Mod::get()->getSavedValue<std::string>("token")))
        .header("X-Submission-Note", std::move(note))
        .body(std::move(readRes).unwrap())
        .userAgent(USER_AGENT)
        .post(fmt::format("{}/upload/{}", Settings::thumbnailAPIBaseURL(), levelID));

    auto code = res.code();

    if (code == 201 || code == 200) {
        co_return Ok("The thumbnail has been applied.");
    } else if (code == 202) {
        co_return Ok("The thumbnail has been submitted, and is now in the queue for approval.");
    }

    if (code == 401 || code == 498) Mod::get()->getSaveContainer().erase("token");
    else if (code == 423) {
        auto reason_res = res.json().unwrapOrDefault()["reason"].asString();
        if (reason_res) {
            co_return Err(fmt::format(
            "Submissions are currently locked for this level.\n<cy>Reason: {}</c>",
                res.json().unwrapOrDefault()["reason"].asString().unwrapOr("auth error")
            ));
        } else {
            co_return Err("Submissions are currently locked for this level.");
        }
    }
    log::error("{}", res.string().unwrapOrDefault());
    co_return Err(fmt::format(
        "<cr>Thumbnail upload failed: {}</c>",
        res.json().unwrapOrDefault()["message"].asString().unwrapOr("auth error")
    ));
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
        co_return Ok("Your account was linked successfully.");
    }

    log::error("Account link failed: {}", res.string().unwrapOrDefault());
    co_return Err(fmt::format(
        "Account link failed: {}",
        res.json().unwrapOrDefault()["message"].asString().unwrapOr("auth error")
    ));
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
                log::error("Session check failed: {}", res.string().unwrapOrDefault());
                m_myRole = getRoleByName(Mod::get()->getSavedValue<std::string>("cached_role"));
                return;
            }

            auto json = res.json().unwrapOrDefault();
            auto role = json["user"]["role"].asString().unwrapOr("user");
            m_myRole = getRoleByName(role);

            Mod::get()->setSavedValue<std::string>("cached_role", role);
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
        co_return Err("badge fetch failed");
    }

    auto role = res.json().unwrapOrDefault()["data"]["role"].asString().unwrapOrDefault();
    auto role_enum = getRoleByName(role);
    m_badgeCache[accountID] = role_enum;

    co_return Ok(role_enum);
}

AuthManager::LoginFuture AuthManager::login() {
    if (GJAccountManager::get()->m_accountID == 0) {
        co_return Err("not logged into an account");
    }

    auto data = co_await async::waitForMainThread<argon::AccountData>([] {
        return argon::getGameAccountData();
    });

    if (!data->valid()) {
        co_return Err("argon - invalid game data");
    }

    auto argonRes = co_await argon::startAuth(data.value());
    if (!argonRes) {
        co_return Err(std::move(argonRes).unwrapErr());
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
        log::error("Login request failed: {}", res.errorMessage());
        co_return Err("login request failed");
    }

    Mod::get()->setSavedValue<std::string>(
        "token", res.json().unwrapOrDefault()["token"].asString().unwrapOrDefault()
    );

    co_return Ok("success!");
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