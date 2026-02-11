
#ifndef KRAKATOA_ASSET_LOADER_H
#define KRAKATOA_ASSET_LOADER_H
#include <android/asset_manager.h>
#include <vector>
#include <string>
#include <cstdint>
/**
 * AssetLoader - Load files from Android APK assets using AAssetManager
 *
 * Android apps can't access files directly - assets are packaged in the APK.
 * This class wraps AAssetManager to load shaders, models, etc.
 */
namespace io {
    class AssetLoader {
    public:
        /**
         * Initialize with Android's AssetManager
         * Get this from Java: getAssets() -> pass to native via JNI
         */
        static void initialize(AAssetManager *assetManager);

        /**
         * Check if asset loader is initialized
         */
        static bool isInitialized();

        /**
         * Load entire file into memory
         * @param path Path relative to assets/ folder (e.g. "shaders/hello.vert.spv")
         * @return File contents, or empty vector if failed
         */
        static std::vector<uint8_t> loadFile(const std::string &path);

        /**
         * Load text file as string
         * @param path Path relative to assets/ folder
         * @return File contents as string, or empty if failed
         */
        static std::string loadTextFile(const std::string &path);

        /**
         * Check if an asset exists
         * @param path Path relative to assets/ folder
         * @return true if asset exists
         */
        static bool exists(const std::string &path);

        /**
         * Set external storage path for DICOM data
         * Used for loading data from external storage instead of APK assets
         * @param path Absolute path to external storage directory
         */
        static void setExternalStoragePath(const std::string &path);

        /**
         * Get external storage path
         * @return External storage path, or empty string if not set
         */
        static std::string getExternalStoragePath();

    private:
        static AAssetManager *s_assetManager;
        static std::string s_externalStoragePath;
    };

}
#endif //KRAKATOA_ASSET_LOADER_H
