#ifndef KRAKATOA_AR_MANAGER_H
#define KRAKATOA_AR_MANAGER_H
#include "ar_loader.h"
#include <jni.h>
#include <functional>
#include <cstdint>
#include <vector>

namespace ar {

    /// Raw YUV camera frame data (CPU-side, no GL_TEXTURE_EXTERNAL_OES)
    struct CameraFrame {
        const uint8_t* yPlane = nullptr;
        const uint8_t* uvPlane = nullptr;  // interleaved UV (NV21/NV12)
        int32_t width = 0;
        int32_t height = 0;
        int32_t yRowStride = 0;
        int32_t uvRowStride = 0;
        int32_t uvPixelStride = 0;         // 1 = NV21/NV12, 2 = planar
        bool valid = false;
    };

    class ARSessionManager {
    public:
        bool initialize(JNIEnv* env, jobject context, jobject activity);
        void onPause();
        void onResume();
        void onDrawFrame();
        void onSurfaceChanged(int rotation, int width, int height);

        bool isTracking() const { return m_isTracking; }
        int  getDisplayRotation() const { return m_displayRotation; }
        int  getDisplayWidth()    const { return m_displayWidth; }
        int  getDisplayHeight()   const { return m_displayHeight; }

        /// Access the latest camera frame (valid until next onDrawFrame call)
        const CameraFrame& getCameraFrame() const { return m_cameraFrame; }

        void forEachPlane(
                const std::function<void(
                        int64_t planeId,
                        const float* modelMatrix,
                        const float* polygon,
                        int polygonFloatCount
                )>& fn);

        void getViewMatrix(float* outMatrix);
        void getProjectionMatrix(float nearClip, float farClip, float* outMatrix);

    private:
        void releaseCameraImage();

        ar::ARCoreLoader& m_loader = ar::ARCoreLoader::getInstance();
        ArTrackableList* m_planeList = nullptr;
        ArSession* m_session = nullptr;
        ArFrame* m_frame = nullptr;
        ArConfig* m_config = nullptr;
        ArImage* m_cameraImage = nullptr;   // current frame's CPU image

        CameraFrame m_cameraFrame{};

        int m_displayWidth = 0;
        int m_displayHeight = 0;
        int m_displayRotation = 0;

        bool m_isTracking = false;
    };
}
#endif //KRAKATOA_AR_MANAGER_H
