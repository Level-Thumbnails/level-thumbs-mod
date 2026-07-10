#pragma once
#include <shared_mutex>
#include <Geode/Geode.hpp>

#define USER_AGENT "LevelThumbnails/" GEODE_PLATFORM_NAME "/" MOD_VERSION

class ThumbnailManager {
private:
    ThumbnailManager();

public:
    ThumbnailManager(ThumbnailManager const&) = delete;
    ThumbnailManager(ThumbnailManager&&) = delete;
    ThumbnailManager& operator=(ThumbnailManager const&) = delete;
    ThumbnailManager& operator=(ThumbnailManager&&) = delete;

    static ThumbnailManager& get();

    enum class Quality {
        Small,
        Medium,
        High
    };

    enum class CacheState {
        NotCached,
        SavedOnDisk,
        Ready
    };

    using FetchResult = geode::Result<geode::Ref<cocos2d::CCTexture2D>>;
    using FetchFuture = arc::Future<FetchResult>;
    using Progress = geode::utils::web::WebProgress const&;
    using ProgressCallback = geode::Function<void(Progress)>;

    using ThumbnailKey = geode::utils::StringBuffer<128>;

    std::optional<geode::Ref<cocos2d::CCTexture2D>> getThumbnail(int32_t levelID, Quality quality = Quality::High);
    FetchFuture fetchThumbnail(int32_t levelID, Quality quality = Quality::High, ProgressCallback progress = nullptr);
    CacheState getCacheState(int32_t levelID, Quality quality = Quality::High);

    static std::string getThumbnailUrl(int32_t levelID, Quality quality = Quality::High);
    static std::filesystem::path getThumbnailPath(int32_t levelID, Quality quality = Quality::High);
    static std::filesystem::path getThumbnailPath(std::string_view api, int32_t levelID, Quality quality);

    static std::filesystem::path const& getCacheDirectory();

    void cleanupFileCacheEntry(int32_t levelID, Quality quality);

    void saveDiskCache();
    void purgeCaches();
    void purgeRuntimeCache();

private:
    static ThumbnailKey getThumbnailKey(int32_t levelID, Quality quality);

    using DecodeResult = geode::Result<cocos2d::CCImage*>;

    geode::Result<cocos2d::CCImage*> readImageFromFile(int32_t levelID, Quality quality);
    static cocos2d::CCImage* decodeImage(std::vector<uint8_t> data);

    void createTexture(
        cocos2d::CCImage* img,
        int32_t levelID, Quality quality,
        arc::oneshot::Sender<geode::Result<geode::Ref<cocos2d::CCTexture2D>>> tx
    );

    void touch(std::string_view key, bool fileCache = false);
    void evictIfNeeded();
    void evictDiskCache();

    struct CacheEntry {
        geode::Ref<cocos2d::CCTexture2D> texture;
        std::chrono::steady_clock::time_point lastAccess;
    };

    struct FileCacheEntry {
        int32_t levelID;
        Quality quality;
        std::string host;
        std::chrono::system_clock::time_point lastAccess;
    };

private:
    geode::utils::StringMap<CacheEntry> m_thumbnailCache;
    geode::utils::StringMap<FileCacheEntry> m_fileCache;
    std::shared_mutex m_cacheMutex;
    std::shared_mutex m_fileCacheMutex;
    bool m_scheduledEviction = false;
};