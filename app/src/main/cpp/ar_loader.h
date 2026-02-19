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
            LOAD_ARCORE_FUNC(ArSession_setDisplayGeometry);

            // Config functions
            LOAD_ARCORE_FUNC(ArConfig_create);
            LOAD_ARCORE_FUNC(ArConfig_destroy);

            // Frame functions
            LOAD_ARCORE_FUNC(ArFrame_create);
            LOAD_ARCORE_FUNC(ArFrame_destroy);
            LOAD_ARCORE_FUNC(ArFrame_getTimestamp);
            LOAD_ARCORE_FUNC(ArFrame_transformCoordinates2d);
            LOAD_ARCORE_FUNC(ArFrame_acquireCamera);
            LOAD_ARCORE_FUNC(ArFrame_acquireCameraImage);

            // Camera functions
            LOAD_ARCORE_FUNC(ArCamera_getViewMatrix);
            LOAD_ARCORE_FUNC(ArCamera_getProjectionMatrix);
            LOAD_ARCORE_FUNC(ArCamera_getTrackingState);
            LOAD_ARCORE_FUNC(ArCamera_release);

            // Image functions (CPU image path)
            LOAD_ARCORE_FUNC(ArImage_getWidth);
            LOAD_ARCORE_FUNC(ArImage_getHeight);
            LOAD_ARCORE_FUNC(ArImage_getNumberOfPlanes);
            LOAD_ARCORE_FUNC(ArImage_getPlaneData);
            LOAD_ARCORE_FUNC(ArImage_getPlaneRowStride);
            LOAD_ARCORE_FUNC(ArImage_getPlanePixelStride);
            LOAD_ARCORE_FUNC(ArImage_release);

            // Pose functions
            LOAD_ARCORE_FUNC(ArPose_create);
            LOAD_ARCORE_FUNC(ArPose_destroy);
            LOAD_ARCORE_FUNC(ArPose_getPoseRaw);
            LOAD_ARCORE_FUNC(ArPose_getMatrix);

            // Trackable list functions
            LOAD_ARCORE_FUNC(ArTrackableList_create);
            LOAD_ARCORE_FUNC(ArTrackableList_destroy);
            LOAD_ARCORE_FUNC(ArTrackableList_getSize);
            LOAD_ARCORE_FUNC(ArTrackableList_acquireItem);
            LOAD_ARCORE_FUNC(ArSession_getAllTrackables);

            // Trackable functions
            LOAD_ARCORE_FUNC(ArTrackable_release);
            LOAD_ARCORE_FUNC(ArTrackable_getTrackingState);

            // Plane functions
            LOAD_ARCORE_FUNC(ArPlane_acquireSubsumedBy);
            LOAD_ARCORE_FUNC(ArPlane_getPolygonSize);
            LOAD_ARCORE_FUNC(ArPlane_getPolygon);
            LOAD_ARCORE_FUNC(ArPlane_getCenterPose);

            LOAD_ARCORE_FUNC(ArSession_setCameraTextureName);

            // Camera config selection
            LOAD_ARCORE_FUNC(ArCameraConfigList_create);
            LOAD_ARCORE_FUNC(ArCameraConfigList_destroy);
            LOAD_ARCORE_FUNC(ArCameraConfigList_getSize);
            LOAD_ARCORE_FUNC(ArCameraConfigList_getItem);
            LOAD_ARCORE_FUNC(ArCameraConfig_create);
            LOAD_ARCORE_FUNC(ArCameraConfig_destroy);
            LOAD_ARCORE_FUNC(ArCameraConfig_getImageDimensions);
            LOAD_ARCORE_FUNC(ArCameraConfigFilter_create);
            LOAD_ARCORE_FUNC(ArCameraConfigFilter_destroy);
            LOAD_ARCORE_FUNC(ArSession_getSupportedCameraConfigsWithFilter);
            LOAD_ARCORE_FUNC(ArSession_setCameraConfig);

#undef LOAD_ARCORE_FUNC

            LOGI("ARCore library loaded successfully");
            return true;
        }

        ~ARCoreLoader() {
            if (handle_) {
                dlclose(handle_);
            }
        }

        // ── Core session ──
        ArStatus (*ArSession_create)(void* env, void* context, ArSession** out_session) = nullptr;
        void (*ArSession_destroy)(ArSession* session) = nullptr;
        ArStatus (*ArSession_pause)(ArSession* session) = nullptr;
        ArStatus (*ArSession_resume)(ArSession* session) = nullptr;
        ArStatus (*ArSession_update)(ArSession* session, ArFrame* out_frame) = nullptr;
        ArStatus (*ArSession_configure)(ArSession* session, const ArConfig* config) = nullptr;
        void (*ArSession_setDisplayGeometry)(ArSession* session, int32_t rotation,
                                             int32_t width, int32_t height) = nullptr;

        // ── Config ──
        ArStatus (*ArConfig_create)(const ArSession* session, ArConfig** out_config) = nullptr;
        void (*ArConfig_destroy)(ArConfig* config) = nullptr;

        // ── Frame ──
        ArStatus (*ArFrame_create)(const ArSession* session, ArFrame** out_frame) = nullptr;
        void (*ArFrame_destroy)(ArFrame* frame) = nullptr;
        int64_t (*ArFrame_getTimestamp)(const ArSession* session, const ArFrame* frame) = nullptr;
        ArStatus (*ArFrame_transformCoordinates2d)(const ArSession* session, const ArFrame* frame,
                                                   ArCoordinates2dType input_type, int32_t number_of_vertices,
                                                   const float* vertices_2d, ArCoordinates2dType output_type,
                                                   float* out_vertices_2d) = nullptr;
        void (*ArFrame_acquireCamera)(const ArSession* session, const ArFrame* frame,
                                      ArCamera** out_camera) = nullptr;
        ArStatus (*ArFrame_acquireCameraImage)(const ArSession* session, const ArFrame* frame,
                                               ArImage** out_image) = nullptr;

        // ── Camera ──
        void (*ArCamera_getViewMatrix)(const ArSession* session, const ArCamera* camera,
                                       float* out_col_major_4x4) = nullptr;
        void (*ArCamera_getProjectionMatrix)(const ArSession* session, const ArCamera* camera,
                                             float near, float far, float* dest_col_major_4x4) = nullptr;
        void (*ArCamera_getTrackingState)(const ArSession* session, const ArCamera* camera,
                                          ArTrackingState* out_tracking_state) = nullptr;
        void (*ArCamera_release)(ArCamera* camera) = nullptr;

        // ── Image (CPU path) ──
        void (*ArImage_getWidth)(const ArSession* session, const ArImage* image,
                                 int32_t* out_width) = nullptr;
        void (*ArImage_getHeight)(const ArSession* session, const ArImage* image,
                                  int32_t* out_height) = nullptr;
        void (*ArImage_getNumberOfPlanes)(const ArSession* session, const ArImage* image,
                                          int32_t* out_num_planes) = nullptr;
        void (*ArImage_getPlaneData)(const ArSession* session, const ArImage* image,
                                     int32_t plane_index, const uint8_t** out_data,
                                     int32_t* out_data_length) = nullptr;
        void (*ArImage_getPlaneRowStride)(const ArSession* session, const ArImage* image,
                                          int32_t plane_index, int32_t* out_row_stride) = nullptr;
        void (*ArImage_getPlanePixelStride)(const ArSession* session, const ArImage* image,
                                            int32_t plane_index, int32_t* out_pixel_stride) = nullptr;
        void (*ArImage_release)(const ArImage* image) = nullptr;

        // ── Pose ──
        ArStatus (*ArPose_create)(const ArSession* session, const float* pose_raw,
                                  ArPose** out_pose) = nullptr;
        void (*ArPose_destroy)(ArPose* pose) = nullptr;
        void (*ArPose_getPoseRaw)(const ArSession* session, const ArPose* pose,
                                  float* out_pose_raw) = nullptr;
        void (*ArPose_getMatrix)(const ArSession* session, const ArPose* pose,
                                 float* out_col_major_4x4) = nullptr;

        // ── Trackable list ──
        void (*ArTrackableList_create)(const ArSession* session,
                                       ArTrackableList** out_trackable_list) = nullptr;
        void (*ArTrackableList_destroy)(ArTrackableList* trackable_list) = nullptr;
        void (*ArTrackableList_getSize)(const ArSession* session, const ArTrackableList* trackable_list,
                                        int32_t* out_size) = nullptr;
        void (*ArTrackableList_acquireItem)(const ArSession* session, const ArTrackableList* trackable_list,
                                            int32_t index, ArTrackable** out_trackable) = nullptr;
        void (*ArSession_getAllTrackables)(const ArSession* session, ArTrackableType filter_type,
                                           ArTrackableList* out_trackable_list) = nullptr;

        // ── Trackable ──
        void (*ArTrackable_release)(ArTrackable* trackable) = nullptr;
        void (*ArTrackable_getTrackingState)(const ArSession* session, const ArTrackable* trackable,
                                             ArTrackingState* out_tracking_state) = nullptr;

        // ── Plane ──
        void (*ArPlane_acquireSubsumedBy)(const ArSession* session, const ArPlane* plane,
                                          ArPlane** out_subsumed_by) = nullptr;
        void (*ArPlane_getPolygonSize)(const ArSession* session, const ArPlane* plane,
                                       int32_t* out_polygon_size) = nullptr;
        void (*ArPlane_getPolygon)(const ArSession* session, const ArPlane* plane,
                                   float* out_polygon_xz) = nullptr;
        void (*ArPlane_getCenterPose)(const ArSession* session, const ArPlane* plane,
                                      ArPose* out_pose) = nullptr;

        void (*ArSession_setCameraTextureName)(ArSession* session, uint32_t texture_id) = nullptr;

        // ── Camera config selection ──
        void (*ArCameraConfigList_create)(const ArSession* session,
                                          ArCameraConfigList** out_list) = nullptr;
        void (*ArCameraConfigList_destroy)(ArCameraConfigList* list) = nullptr;
        void (*ArCameraConfigList_getSize)(const ArSession* session,
                                           const ArCameraConfigList* list,
                                           int32_t* out_size) = nullptr;
        void (*ArCameraConfigList_getItem)(const ArSession* session,
                                           const ArCameraConfigList* list,
                                           int32_t index,
                                           ArCameraConfig* out_camera_config) = nullptr;
        void (*ArCameraConfig_create)(const ArSession* session,
                                      ArCameraConfig** out_camera_config) = nullptr;
        void (*ArCameraConfig_destroy)(ArCameraConfig* camera_config) = nullptr;
        void (*ArCameraConfig_getImageDimensions)(const ArSession* session,
                                                   const ArCameraConfig* camera_config,
                                                   int32_t* out_width,
                                                   int32_t* out_height) = nullptr;
        void (*ArCameraConfigFilter_create)(const ArSession* session,
                                            ArCameraConfigFilter** out_filter) = nullptr;
        void (*ArCameraConfigFilter_destroy)(ArCameraConfigFilter* filter) = nullptr;
        void (*ArSession_getSupportedCameraConfigsWithFilter)(
                const ArSession* session,
                const ArCameraConfigFilter* filter,
                ArCameraConfigList* list) = nullptr;
        ArStatus (*ArSession_setCameraConfig)(const ArSession* session,
                                              const ArCameraConfig* camera_config) = nullptr;

    private:
        ARCoreLoader() = default;
        void* handle_ = nullptr;
    };

    /**
     * Inits ARCoreLoader instance, loading the library and the function pointers,
     * returning false if something is wrong.
     * */
    inline bool LoadARCore(){
        LOGI("Loading ARCORE");
        auto& arcoreInstance = ar::ARCoreLoader::getInstance();
        bool loadedArcore = arcoreInstance.loadARCore();
        LOGI("Loaded ARCORE");
        return loadedArcore;
    }

} // namespace ar
#endif //KRAKATOA_AR_LOADER_H