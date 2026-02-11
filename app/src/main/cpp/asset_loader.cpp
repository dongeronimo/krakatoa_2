#include "asset_loader.h"
#include "android_log.h"
namespace  io {
    AAssetManager *AssetLoader::s_assetManager = nullptr;
    std::string AssetLoader::s_externalStoragePath;

    void AssetLoader::initialize(AAssetManager *assetManager) {
        s_assetManager = assetManager;
        LOGI("AssetLoader initialized");
    }

    bool AssetLoader::isInitialized() {
        return s_assetManager != nullptr;
    }

    std::vector<uint8_t> AssetLoader::loadFile(const std::string &path) {
        if (!s_assetManager) {
            LOGE("AssetManager not initialized! Call initialize() first.");
            return {};
        }

        // Open asset
        AAsset *asset = AAssetManager_open(s_assetManager, path.c_str(), AASSET_MODE_BUFFER);
        if (!asset) {
            LOGE("Failed to open asset: %s", path.c_str());
            return {};
        }

        // Get file size
        off_t size = AAsset_getLength(asset);
        if (size <= 0) {
            LOGE("Asset has invalid size: %s (size: %ld)", path.c_str(), size);
            AAsset_close(asset);
            return {};
        }

        // Read file contents
        std::vector<uint8_t> buffer(size);
        int bytesRead = AAsset_read(asset, buffer.data(), size);
        AAsset_close(asset);

        if (bytesRead != size) {
            LOGE("Failed to read full asset: %s (read %d of %ld bytes)", path.c_str(), bytesRead,
                 size);
            return {};
        }

        LOGI("Loaded asset: %s (%ld bytes)", path.c_str(), size);
        return buffer;
    }

    std::string AssetLoader::loadTextFile(const std::string &path) {
        std::vector<uint8_t> data = loadFile(path);
        if (data.empty()) {
            return "";
        }

        return std::string(data.begin(), data.end());
    }

    bool AssetLoader::exists(const std::string &path) {
        if (!s_assetManager) {
            return false;
        }

        AAsset *asset = AAssetManager_open(s_assetManager, path.c_str(), AASSET_MODE_BUFFER);
        if (asset) {
            AAsset_close(asset);
            return true;
        }

        return false;
    }

    void AssetLoader::setExternalStoragePath(const std::string &path) {
        s_externalStoragePath = path;
        LOGI("External storage path set to: %s", path.c_str());
    }

    std::string AssetLoader::getExternalStoragePath() {
        return s_externalStoragePath;
    }
}