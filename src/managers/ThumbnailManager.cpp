#include "ThumbnailManager.hpp"
#include "SettingsManager.hpp"

#include <asp/fs.hpp>
#include <Geode/utils/web.hpp>
// #define DISABLE_FILE_CACHING 1

using namespace geode::prelude;

std::string_view format_as(ThumbnailManager::Quality quality) {
    switch (quality) {
        case ThumbnailManager::Quality::Small: return "small";
        case ThumbnailManager::Quality::Medium: return "medium";
        case ThumbnailManager::Quality::High: default: return "high";
    }
}

ThumbnailManager::ThumbnailManager() {
#ifndef DISABLE_FILE_CACHING
    // load file cache
    auto savedCache = Mod::get()->getSavedValue<matjson::Value>("cache");

    auto const readEntry = [](matjson::Value const& val, std::string const& host) -> Result<FileCacheEntry> {
        GEODE_UNWRAP_INTO(int32_t levelID, val["levelID"].asInt());
        GEODE_UNWRAP_INTO(int qualityInt, val["quality"].asInt());
        if (qualityInt < 0 || qualityInt > 2) {
            return Err("Invalid quality in cache");
        }
        auto quality = static_cast<Quality>(qualityInt);
        GEODE_UNWRAP_INTO(uint64_t timestamp, val["lastAccess"].asInt());

        if (!asp::fs::exists(getThumbnailPath(host, levelID, quality))) {
            return Err("Cached file does not exist");
        }

        return Ok(FileCacheEntry{
            levelID,
            quality,
            host,
            std::chrono::system_clock::time_point(std::chrono::seconds(timestamp)),
        });
    };

    for (auto const& [host, entries] : savedCache) {
        for (auto const& val : entries) {
            auto res = readEntry(val, host);
            if (!res) {
                log::warn("Failed to read thumbnail cache entry: {}", res.unwrapErr());
                continue;
            }
            auto entry = std::move(res).unwrap();
            m_fileCache[fmt::format("{}-{}-{}", entry.levelID, entry.quality, entry.host)] = std::move(entry);
        }
    }

    evictDiskCache();

    // remove stray files
    std::error_code ec;
    for (auto const& dir : std::filesystem::directory_iterator(Mod::get()->getSaveDir() / "cache", ec)) {
        for (auto const& f : std::filesystem::directory_iterator(dir, ec)) {
            bool referenced = std::ranges::any_of(m_fileCache,
                [&](auto const& kv) {
                    return getThumbnailPath(kv.second.host, kv.second.levelID, kv.second.quality) == f.path();
                }
            );
            if (!referenced) {
                std::filesystem::remove(f.path(), ec);
            }
        }
    }
#endif
}

ThumbnailManager& ThumbnailManager::get() {
    static ThumbnailManager instance;
    return instance;
}

std::optional<Ref<CCTexture2D>> ThumbnailManager::getThumbnail(int32_t levelID, Quality quality) {
    auto key = getThumbnailKey(levelID, quality);
    std::shared_lock lock(m_cacheMutex);
    auto it = m_thumbnailCache.find(key.view());
    if (it != m_thumbnailCache.end()) {
        this->touch(it->first);
        return it->second.texture;
    }
    return std::nullopt;
}

ThumbnailManager::FetchFuture ThumbnailManager::fetchThumbnail(int32_t levelID, Quality quality, ProgressCallback progress) {
    auto key = getThumbnailKey(levelID, quality);

    // check memory cache
    {
        std::shared_lock lock(m_cacheMutex);
        auto it = m_thumbnailCache.find(key.view());
        if (it != m_thumbnailCache.end()) {
            this->touch(it->first);
            co_return Ok(it->second.texture);
        }
    }

    // check file cache
#ifndef DISABLE_FILE_CACHING
    bool inFileCache = false;
    {
        std::shared_lock lock(m_fileCacheMutex);
        auto it2 = m_fileCache.find(key.view());
        if (it2 != m_fileCache.end()) {
            this->touch(it2->first, true);
            inFileCache = true;
        }
    }

    if (inFileCache) {
        GEODE_CO_UNWRAP_INTO(
            auto img,
            co_await async::runtime().spawnBlocking<Result<CCImage*>>(
                [this, levelID, quality] {
                    return this->readImageFromFile(levelID, quality);
                }
            )
        );

        if (!img) {
            co_return Err("Failed to decode image");
        }

        auto [tx, rx] = arc::oneshot::channel<FetchResult>();
        queueInMainThread([
            this, img,
            levelID, quality,
            tx = std::move(tx)
        ]() mutable {
            this->createTexture(
                img,
                levelID, quality,
                std::move(tx)
            );
        });

        auto res = co_await rx.recv();
        if (!res) {
            this->cleanupFileCacheEntry(levelID, quality);
            co_return Err("Failed to create texture from cached thumbnail");
        }

        co_return std::move(res).unwrap();
    }
#endif

    auto req = web::WebRequest().userAgent(USER_AGENT);
    if (progress) {
        req.onProgress(std::move(progress));
    }

    auto res = co_await req.get(getThumbnailUrl(levelID, quality));

    if (!res.ok()) {
        switch (res.code()) {
            default: co_return Err(res.errorMessage());
            case 404: co_return Err("Thumbnail not found");
            case 500: co_return Err(
                res.json().unwrapOrDefault()["message"].asString().unwrapOr("Internal server error")
            );
        }
    }

    auto img = co_await async::runtime().spawnBlocking<CCImage*>(
        [this, levelID, quality, data = std::move(res).data(), key = std::move(key)] mutable {
            #ifndef DISABLE_FILE_CACHING
            if (Settings::thumbnailFileCacheLimit() != 0) {
                auto path = getThumbnailPath(levelID, quality);
                auto writeRes = file::writeBinary(path, data);
                if (!writeRes) {
                    log::warn("Failed to write thumbnail {} to disk cache: {}", path, writeRes.unwrapErr());
                } else {
                    std::unique_lock lock(m_fileCacheMutex);
                    m_fileCache[key.str()] = {
                        levelID,
                        quality,
                        std::string(Settings::thumbnailAPIBaseURL()),
                        std::chrono::system_clock::now(),
                    };
                }
            }
            #endif

            return decodeImage(std::move(data));
        }
    );

    if (!img) {
        co_return Err("Failed to decode image");
    }

    auto [tx, rx] = arc::oneshot::channel<FetchResult>();
    queueInMainThread([
        this, img,
        levelID, quality,
        tx = std::move(tx)
    ]() mutable {
        this->createTexture(
            img,
            levelID, quality,
            std::move(tx)
        );
    });

    auto decodeRes = co_await rx.recv();
    if (!decodeRes) {
        this->cleanupFileCacheEntry(levelID, quality);
        co_return Err("Failed to create texture from downloaded thumbnail");
    }

    co_return std::move(decodeRes).unwrap();
}

ThumbnailManager::CacheState ThumbnailManager::getCacheState(int32_t levelID, Quality quality) {
    auto key = getThumbnailKey(levelID, quality);
    {
        std::shared_lock lock(m_cacheMutex);
        if (m_thumbnailCache.contains(key.view())) {
            return CacheState::Ready;
        }
    }

    // check disk cache
#ifndef DISABLE_FILE_CACHING
    std::shared_lock lock(m_fileCacheMutex);
    if (m_fileCache.contains(key.view())) {
        return CacheState::SavedOnDisk;
    }
#endif

    return CacheState::NotCached;
}

std::string ThumbnailManager::getThumbnailUrl(int32_t levelID, Quality quality) {
    if (Settings::isLegacyAPI()) {
        return fmt::format("{}/{}.png", Settings::thumbnailAPIBaseURL(), levelID);
    }
    if (quality == Quality::High) {
        return fmt::format("{}/thumbnail/{}", Settings::thumbnailAPIBaseURL(), levelID);
    }
    return fmt::format("{}/thumbnail/{}/{}", Settings::thumbnailAPIBaseURL(), levelID, quality);
}

#ifndef DISABLE_FILE_CACHING
std::filesystem::path ThumbnailManager::getThumbnailPath(int32_t levelID, Quality quality) {
    // i know legacy api uses png, but tracking separate extensions is harder
    return getCacheDirectory() / fmt::format("{}-{}.webp", levelID, quality);
}

std::filesystem::path ThumbnailManager::getThumbnailPath(std::string_view api, int32_t levelID, Quality quality) {
    return Mod::get()->getSaveDir()
         / "cache"
         / fmt::to_string(std::hash<std::string_view>{}(api))
         / fmt::format("{}-{}.webp", levelID, quality);
}

std::filesystem::path const& ThumbnailManager::getCacheDirectory() {
    static std::filesystem::path path = (
        listenForSettingChanges<std::string>("level-thumbnails-api", [](std::string val) {
            path = Mod::get()->getSaveDir() / "cache" / fmt::to_string(std::hash<std::string_view>{}(val));
        }),
        Mod::get()->getSaveDir() / "cache" / fmt::to_string(std::hash<std::string_view>{}(Settings::thumbnailAPIBaseURL()))
    );

    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        log::warn("Failed to create thumbnail cache directory: {}", ec.message());
    }

    return path;
}

void ThumbnailManager::cleanupFileCacheEntry(int32_t levelID, Quality quality) {
    {
        std::unique_lock lock(m_fileCacheMutex);
        auto key = getThumbnailKey(levelID, quality);
        auto it = m_fileCache.find(key.view());
        if (it == m_fileCache.end()) return;
        m_fileCache.erase(it);
    }

    std::error_code ec;
    std::filesystem::remove(getThumbnailPath(levelID, quality), ec);
    if (ec) {
        log::warn("Failed to remove cached thumbnail {}: {}", getThumbnailPath(levelID, quality), ec.message());
    }
}

void ThumbnailManager::saveDiskCache() {
    std::unique_lock lock(m_fileCacheMutex);
    evictDiskCache();
    if (Settings::thumbnailCacheLimit() == 0) {
        return;
    }

    std::unordered_map<std::string, std::vector<matjson::Value>> grouped;
    for (auto const& [key, entry] : m_fileCache) {
        grouped[entry.host].push_back(matjson::makeObject({
            {"levelID", entry.levelID},
            {"quality", static_cast<int>(entry.quality)},
            {"lastAccess", std::chrono::duration_cast<std::chrono::seconds>(entry.lastAccess.time_since_epoch()).count()}
        }));
    }

    Mod::get()->setSavedValue("cache", grouped);
}
#endif

ThumbnailManager::ThumbnailKey ThumbnailManager::getThumbnailKey(int32_t levelID, Quality quality) {
    ThumbnailKey buf;
    buf.append("{}-{}-{}", levelID, quality, Settings::thumbnailAPIBaseURL());
    return buf;
}

Result<CCImage*> ThumbnailManager::readImageFromFile(int32_t levelID, Quality quality) {
    auto path = getThumbnailPath(levelID, quality);
    auto res = file::readBinary(path);
    if (!res) {
        // delete the corrupted file from cache
        log::warn("Failed to read cached thumbnail '{}': {}", path, res.unwrapErr());
        get().cleanupFileCacheEntry(levelID, quality);
        return Err("Failed to read cached thumbnail. Retry download");
    }

    auto data = std::move(res).unwrap();
    if (data.empty()) {
        // delete the corrupted file from cache
        log::warn("Cached thumbnail is empty");
        get().cleanupFileCacheEntry(levelID, quality);
        return Err("Cached thumbnail is empty. Retry download");
    }

    return Ok(decodeImage(std::move(data)));
}

CCImage* ThumbnailManager::decodeImage(std::vector<uint8_t> data) {
    auto img = new CCImage();
    if (!img->initWithImageData(data.data(), data.size())) {
        delete img;
        return nullptr;
    }
    return img;
}

void ThumbnailManager::createTexture(
    CCImage* img,
    int32_t levelID, Quality quality,
    arc::oneshot::Sender<Result<Ref<CCTexture2D>>> tx
) {
    // make sure to delete image on exit
    std::unique_ptr<CCImage> imgPtr(img);

    // create texture
    auto texture = new CCTexture2D();
    if (!texture->initWithImage(img)) {
        delete texture;
        (void) tx.send(Err("Failed to create texture from image"));
        return;
    }

    {
        std::unique_lock lock(m_cacheMutex);
        m_thumbnailCache[getThumbnailKey(levelID, quality).str()] = {
            texture,
            std::chrono::steady_clock::now()
        };
    }

    texture->release();

    (void) tx.send(Ok(texture));

    if (!m_scheduledEviction && m_thumbnailCache.size() > Settings::thumbnailCacheLimit()) {
        queueInMainThread([this] { evictIfNeeded(); });
        m_scheduledEviction = true;
    }
}

void ThumbnailManager::touch(std::string_view key, bool fileCache) {
#ifndef DISABLE_FILE_CACHING
    if (fileCache) {
        auto it = m_fileCache.find(key);
        if (it != m_fileCache.end()) {
            it->second.lastAccess = std::chrono::system_clock::now();
        }
    }
#endif

    auto it = m_thumbnailCache.find(key);
    if (it == m_thumbnailCache.end()) return;

    it->second.lastAccess = std::chrono::steady_clock::now();
}

void ThumbnailManager::purgeCaches() {
    {
        std::unique_lock lock(m_cacheMutex);
        m_thumbnailCache.clear();
    }

    {
        std::unique_lock lock(m_fileCacheMutex);
        m_fileCache.clear();
    }

    std::error_code ec;
    std::filesystem::remove_all(getCacheDirectory(), ec);
    if (ec) {
        log::warn("Failed to clear thumbnail disk cache: {}", ec.message());
    }
}

void ThumbnailManager::purgeRuntimeCache() {
    std::unique_lock lock(m_cacheMutex);
    m_thumbnailCache.clear();
}

void ThumbnailManager::evictIfNeeded() {
    auto maxCacheSize = Settings::thumbnailCacheLimit();
    if (m_thumbnailCache.size() <= maxCacheSize) {
        return;
    }

    // evict least frequently used
    while (m_thumbnailCache.size() > maxCacheSize) {
        auto it = std::ranges::min_element(
            m_thumbnailCache,
            [](auto const& a, auto const& b) {
                return a.second.lastAccess < b.second.lastAccess; // evict least recently used
            }
        );

        if (it == m_thumbnailCache.end()) break; // should not happen but just in case
        m_thumbnailCache.erase(it);
    }

    m_scheduledEviction = false;
}

#ifndef DISABLE_FILE_CACHING
void ThumbnailManager::evictDiskCache() {
    auto limit = Settings::thumbnailFileCacheLimit();

    // no eviction
    if (limit <= -1) {
        return;
    }

    // delete all files
    if (limit == 0) {
        std::error_code ec;
        std::filesystem::remove_all(getCacheDirectory(), ec);
        if (ec) {
            log::warn("Failed to clear thumbnail disk cache: {}", ec.message());
        } else {
            m_fileCache.clear();
        }

        return;
    }

    while (m_fileCache.size() > static_cast<size_t>(limit)) {
        auto it = std::ranges::min_element(
            m_fileCache,
            [](auto const& a, auto const& b) {
                return a.second.lastAccess < b.second.lastAccess;
            }
        );

        if (it == m_fileCache.end()) break;
        std::error_code ec;
        std::filesystem::remove(getThumbnailPath(
            it->second.host,
            it->second.levelID,
            it->second.quality
        ), ec);

        if (ec) {
            log::warn("Failed to remove cached thumbnail file: {}", ec.message());
        }
        m_fileCache.erase(it);
    }
}

$on_mod(DataSaved) {
    ThumbnailManager::get().saveDiskCache();
}
#endif

$on_game(TexturesUnloaded) {
    ThumbnailManager::get().purgeRuntimeCache();
}