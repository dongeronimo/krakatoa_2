#ifndef KRAKATOA_AR_LOADER_H
#define KRAKATOA_AR_LOADER_H
#include "android_log.h"
#include "arcore_c_api.h"
#include <dlfcn.h>
namespace ar {
    /**
     * Loads the ARCore dll and it's functions. ARCore is a dll, can't be statically linked. To
     * use it we must first load the dll and then get the function pointers.
     *
     * The class is a singleton. You get the instance with getInstance(). The 1st time getInstance
     * is invoked the instance object is created.
     *
     * Usage:
     *   auto& loader = ARCoreLoader::getInstance();
     *   if(loader.loadARCore()) {
     *      //rest of the app...
     *   }
     *   */
    class ARCoreLoader {;
    public:
        /**
         * Gets the instance
         * */
        static ARCoreLoader& getInstance() {
            static ARCoreLoader instance;
            return instance;
        }
        /**
         * Try to load the arcore dll and it's functions.
         * */
        bool loadARCore() {
            if (handle_ != nullptr) {
                return true; // Already loaded
            }

            // Try to load ARCore shared library
            handle_ = dlopen("libarcore_sdk_c.so", RTLD_NOW | RTLD_LOCAL);
            if (!handle_) {
                LOGE("Failed to load ARCore library: %s", dlerror());
                return false;
            }

                // Load all ARCore functions
#define LOAD_ARCORE_FUNC(name) \
            name = reinterpret_cast<decltype(name)>(dlsym(handle_, #name)); \
            if (!name) { \
                LOGE("Failed to load function: %s", #name); \
                dlclose(handle_); \
                handle_ = nullptr; \
                return false; \
            }

            // Core session functions
            LOAD_ARCORE_FUNC(ArSession_create);
            LOAD_ARCORE_FUNC(ArSession_destroy);
            LOAD_ARCORE_FUNC(ArSession_pause);
            LOAD_ARCORE_FUNC(ArSession_resume);
            LOAD_ARCORE_FUNC(ArSession_update);
            LOAD_ARCORE_FUNC(ArSession_configure);

            // Frame functions
            LOAD_ARCORE_FUNC(ArFrame_create);
            LOAD_ARCORE_FUNC(ArFrame_destroy);
            LOAD_ARCORE_FUNC(ArFrame_getTimestamp);
            LOAD_ARCORE_FUNC(ArFrame_transformCoordinates2d);

            // Camera functions
            LOAD_ARCORE_FUNC(ArCamera_getViewMatrix);
            LOAD_ARCORE_FUNC(ArCamera_getProjectionMatrix);
            LOAD_ARCORE_FUNC(ArCamera_getTrackingState);

            // Pose functions
            LOAD_ARCORE_FUNC(ArPose_create);
            LOAD_ARCORE_FUNC(ArPose_destroy);
            LOAD_ARCORE_FUNC(ArPose_getPoseRaw);

            LOAD_ARCORE_FUNC(ArConfig_create);
            LOAD_ARCORE_FUNC(ArConfig_destroy);
            LOAD_ARCORE_FUNC(ArSession_setCameraTextureName);
            LOAD_ARCORE_FUNC(ArFrame_acquireCamera);
            LOAD_ARCORE_FUNC(ArCamera_release);
            LOAD_ARCORE_FUNC(ArFrame_transformDisplayUvCoords);
            LOAD_ARCORE_FUNC(ArSession_setDisplayGeometry);

            LOAD_ARCORE_FUNC(ArTrackableList_create);
            LOAD_ARCORE_FUNC(ArSession_getAllTrackables);
            LOAD_ARCORE_FUNC(ArTrackableList_destroy);
            LOAD_ARCORE_FUNC(ArTrackableList_getSize);
            LOAD_ARCORE_FUNC(ArTrackableList_acquireItem);
            LOAD_ARCORE_FUNC(ArTrackable_release);

            LOAD_ARCORE_FUNC(ArTrackable_getTrackingState);
            LOAD_ARCORE_FUNC(ArPlane_acquireSubsumedBy);
            LOAD_ARCORE_FUNC(ArPlane_getPolygonSize);
            LOAD_ARCORE_FUNC(ArPlane_getPolygon);
            LOAD_ARCORE_FUNC(ArPlane_getCenterPose);
            LOAD_ARCORE_FUNC(ArPose_getMatrix);
#undef LOAD_ARCORE_FUNC

            LOGI("ARCore library loaded successfully");
            return true;
        }

        ~ARCoreLoader() {
            if (handle_) {
                dlclose(handle_);
            }
        }

        // Function pointers - declare all ARCore functions you'll use
        ArStatus (*ArSession_create)(void* env, void* context, ArSession** out_session) = nullptr;
        void (*ArSession_destroy)(ArSession* session) = nullptr;
        ArStatus (*ArSession_pause)(ArSession* session) = nullptr;
        ArStatus (*ArSession_resume)(ArSession* session) = nullptr;
        ArStatus (*ArSession_update)(ArSession* session, ArFrame* out_frame) = nullptr;
        ArStatus (*ArSession_configure)(ArSession* session, const ArConfig* config) = nullptr;

        ArStatus (*ArFrame_create)(const ArSession* session, ArFrame** out_frame) = nullptr;
        void (*ArFrame_destroy)(ArFrame* frame) = nullptr;
        int64_t (*ArFrame_getTimestamp)(const ArSession* session, const ArFrame* frame) = nullptr;
        ArStatus (*ArFrame_transformCoordinates2d)(const ArSession* session, const ArFrame* frame,
                                                   ArCoordinates2dType input_type, int32_t number_of_vertices,
                                                   const float* vertices_2d, ArCoordinates2dType output_type,
                                                   float* out_vertices_2d) = nullptr;

        void (*ArCamera_getViewMatrix)(const ArSession* session, const ArCamera* camera, float* out_col_major_4x4) = nullptr;
        void (*ArCamera_getProjectionMatrix)(const ArSession* session, const ArCamera* camera,
                                             float near, float far, float* dest_col_major_4x4) = nullptr;
        void (*ArCamera_getTrackingState)(const ArSession* session, const ArCamera* camera, ArTrackingState* out_tracking_state) = nullptr;

        ArStatus (*ArPose_create)(const ArSession* session, const float* pose_raw, ArPose** out_pose) = nullptr;
        void (*ArPose_destroy)(ArPose* pose) = nullptr;
        void (*ArPose_getPoseRaw)(const ArSession* session, const ArPose* pose, float* out_pose_raw) = nullptr;

        // Config functions
        ArStatus (*ArConfig_create)(const ArSession* session, ArConfig** out_config) = nullptr;
        void (*ArConfig_destroy)(ArConfig* config) = nullptr;

// Session texture functions - THIS IS WHAT YOU NEED
        void (*ArSession_setCameraTextureName)(ArSession* session, uint32_t texture_id) = nullptr;

// Frame camera access
        void (*ArFrame_acquireCamera)(const ArSession* session, const ArFrame* frame, ArCamera** out_camera) = nullptr;
        void (*ArCamera_release)(ArCamera* camera) = nullptr;

// Get the UV transform
        void (*ArFrame_getUpdatedTrackables)(const ArSession* session, const ArFrame* frame,
                                             ArTrackableType filter_type, ArTrackableList* out_trackable_list) = nullptr;

// Display geometry (for the texture transform)
        void (*ArFrame_transformDisplayUvCoords)(const ArSession* session, const ArFrame* frame,
                                                 int32_t num_elements, const float* uvs_in,
                                                 float* uvs_out) = nullptr;
        void (*ArSession_setDisplayGeometry)(ArSession* session, int32_t rotation,
                                             int32_t width, int32_t height) = nullptr;

        /**
         * Creates a trackable list object
         * */
        void (*ArTrackableList_create)(const ArSession *session, ArTrackableList **out_trackable_list);
        void (*ArSession_getAllTrackables)(const ArSession *session,ArTrackableType filter_type,ArTrackableList *out_trackable_list);
        void (*ArTrackableList_destroy)(ArTrackableList*);
        void (*ArTrackableList_getSize)(const ArSession*, const ArTrackableList*, int32_t*);
        void (*ArTrackableList_acquireItem)(const ArSession*, const ArTrackableList*, int32_t, ArTrackable**);
        void (*ArTrackable_release)(ArTrackable*);
        void (*ArPose_getMatrix)(const ArSession* session,const ArPose* pose, float* out_col_major_4x4);
        void (*ArTrackable_getTrackingState)(
                const ArSession* session,
                const ArTrackable* trackable,
                ArTrackingState* out_tracking_state
        ) = nullptr;

        void (*ArPlane_getPolygonSize)(const ArSession*, const ArPlane*, int32_t*);
        void (*ArPlane_getPolygon)(const ArSession*, const ArPlane*, float*);
        void (*ArPlane_getCenterPose)(const ArSession*, const ArPlane*, ArPose*);
        void (*ArPlane_acquireSubsumedBy)(
                const ArSession*,
                const ArPlane*,
                ArPlane**
        ) = nullptr;
    private:
        ARCoreLoader() = default;
        void* handle_ = nullptr;
    };
    /**
     * Inits ARCoreLoader instance, loading the library and the function pointers,
     * returning false if something is wrong.
     * */
    bool LoadARCore(){
        LOGI("Loading ARCORE");
        auto& arcoreInstance = ar::ARCoreLoader::getInstance();
        bool loadedArcore = arcoreInstance.loadARCore();
        LOGI("Loaded ARCORE");
        return loadedArcore;
    }

} // namespace ar
#endif //KRAKATOA_AR_LOADER_H
