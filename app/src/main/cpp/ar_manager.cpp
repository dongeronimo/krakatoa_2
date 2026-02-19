
#include "ar_manager.h"
#include "android_log.h"
#include <cassert>
#include <algorithm>
#include <GLES3/gl3.h>
namespace ar {

    bool ARSessionManager::initialize(JNIEnv* env, jobject context, jobject activity) {
        LOGI("ARSessionManager::initialize - loading ARCore...");
        if (!m_loader.loadARCore()) {
            LOGE("Failed to load ARCore");
            return false;
        }
        LOGI("ARSessionManager::initialize - ARCore loaded");

        // ── No GL texture creation ──
        // We do NOT call ArSession_setCameraTextureName.
        // Camera frames are acquired CPU-side via ArFrame_acquireCameraImage.

        LOGI("ARSessionManager::initialize - creating session...");
        ArStatus status = m_loader.ArSession_create(env, context, &m_session);
        LOGI("ARSessionManager::initialize - ArSession_create returned: %d", status);

        if (status != AR_SUCCESS) {
            LOGE("Failed to create ARCore session: %d", status);
            return false;
        }
        // Dummy GL texture — ARCore exige uma, mas nunca a usamos.
        // A imagem real vem via ArFrame_acquireCameraImage (CPU path).
        GLuint dummyTexture = 0;
        glGenTextures(1, &dummyTexture);
        m_loader.ArSession_setCameraTextureName(m_session, dummyTexture);

        // ── Query available resolutions and select the highest ──
        queryAvailableResolutions();
        if (!m_resolutions.empty()) {
            // Default to highest resolution (last entry, sorted ascending by pixel count)
            setResolution(static_cast<int32_t>(m_resolutions.size()) - 1);
        }

        LOGI("ARSessionManager::initialize - creating config...");
        m_loader.ArConfig_create(m_session, &m_config);

        LOGI("ARSessionManager::initialize - configuring session...");
        status = m_loader.ArSession_configure(m_session, m_config);
        LOGI("ARSessionManager::initialize - configure returned: %d", status);

        LOGI("ARSessionManager::initialize - creating frame...");
        m_loader.ArFrame_create(m_session, &m_frame);

        m_loader.ArTrackableList_create(m_session, &m_planeList);
        assert(m_planeList);
        LOGI("ARSessionManager::initialize - plane list created");

        LOGI("ARSessionManager::initialize - done (CPU image path, no OES texture)");
        return true;
    }

    void ARSessionManager::onResume() {
        if (m_session) {
            ArStatus status = m_loader.ArSession_resume(m_session);
            if (status != AR_SUCCESS) {
                LOGE("Failed to resume ARCore session: %d", status);
            }
        }
    }

    void ARSessionManager::onPause() {
        if (m_session) {
            m_loader.ArSession_pause(m_session);
        }
        releaseCameraImage();
    }

    void ARSessionManager::onSurfaceChanged(int rotation, int width, int height) {
        m_displayRotation = rotation;
        m_displayWidth = width;
        m_displayHeight = height;

        if (m_session) {
            m_loader.ArSession_setDisplayGeometry(m_session, rotation, width, height);
        }
    }

    void ARSessionManager::onDrawFrame() {
        if (!m_session || !m_frame) return;

        // Release previous frame's image
        releaseCameraImage();

        // Update session (drives tracking, plane detection, etc.)
        ArStatus status = m_loader.ArSession_update(m_session, m_frame);
        if (status != AR_SUCCESS) {
            LOGE("ArSession_update failed: %d", status);
            return;
        }

        // Check tracking state
        ArCamera* camera = nullptr;
        m_loader.ArFrame_acquireCamera(m_session, m_frame, &camera);

        ArTrackingState trackingState;
        m_loader.ArCamera_getTrackingState(m_session, camera, &trackingState);
        m_isTracking = (trackingState == AR_TRACKING_STATE_TRACKING);

        m_loader.ArCamera_release(camera);

        // Update plane list
        m_loader.ArSession_getAllTrackables(
                m_session,
                AR_TRACKABLE_PLANE,
                m_planeList
        );

        // ── Acquire CPU camera image ──
        status = m_loader.ArFrame_acquireCameraImage(m_session, m_frame, &m_cameraImage);
        if (status != AR_SUCCESS) {
            // This can fail if the frame doesn't have an image yet (e.g. first frames)
            m_cameraFrame = {};
            return;
        }

        // Extract image dimensions
        m_loader.ArImage_getWidth(m_session, m_cameraImage, &m_cameraFrame.width);
        m_loader.ArImage_getHeight(m_session, m_cameraImage, &m_cameraFrame.height);

        // Y plane (index 0)
        int32_t yLength = 0;
        m_loader.ArImage_getPlaneData(
                m_session, m_cameraImage,
                0,  // Y plane
                &m_cameraFrame.yPlane,
                &yLength
        );
        m_loader.ArImage_getPlaneRowStride(
                m_session, m_cameraImage,
                0,
                &m_cameraFrame.yRowStride
        );

        // UV plane (index 2 for NV21 interleaved — ARCore typically gives NV21)
        // Plane index 1 = U, index 2 = V, but with NV21 the pixel stride is 2
        // meaning they're interleaved. We grab plane 1 (U) which covers both.
        int32_t uvLength = 0;
        m_loader.ArImage_getPlaneData(
                m_session, m_cameraImage,
                1,  // U plane (interleaved with V when pixelStride == 2)
                &m_cameraFrame.uvPlane,
                &uvLength
        );
        m_loader.ArImage_getPlaneRowStride(
                m_session, m_cameraImage,
                1,
                &m_cameraFrame.uvRowStride
        );
        m_loader.ArImage_getPlanePixelStride(
                m_session, m_cameraImage,
                1,
                &m_cameraFrame.uvPixelStride
        );

        m_cameraFrame.valid = true;
    }

    void ARSessionManager::releaseCameraImage() {
        if (m_cameraImage) {
            m_loader.ArImage_release(m_cameraImage);
            m_cameraImage = nullptr;
        }
        m_cameraFrame = {};
    }

    void ARSessionManager::getViewMatrix(float* outMatrix) {
        ArCamera* camera = nullptr;
        m_loader.ArFrame_acquireCamera(m_session, m_frame, &camera);
        m_loader.ArCamera_getViewMatrix(m_session, camera, outMatrix);
        m_loader.ArCamera_release(camera);
    }

    void ARSessionManager::queryAvailableResolutions() {
        m_resolutions.clear();
        m_currentResolutionIndex = -1;

        ArCameraConfigFilter* filter = nullptr;
        m_loader.ArCameraConfigFilter_create(m_session, &filter);

        ArCameraConfigList* configList = nullptr;
        m_loader.ArCameraConfigList_create(m_session, &configList);

        m_loader.ArSession_getSupportedCameraConfigsWithFilter(
                m_session, filter, configList);

        int32_t numConfigs = 0;
        m_loader.ArCameraConfigList_getSize(m_session, configList, &numConfigs);

        ArCameraConfig* tempConfig = nullptr;
        m_loader.ArCameraConfig_create(m_session, &tempConfig);

        // Collect unique resolutions, deduplicate (ARCore may return
        // multiple configs with the same CPU resolution but different
        // GPU texture / FPS / depth settings).
        struct Seen { int32_t w, h; };
        std::vector<Seen> seen;

        for (int32_t i = 0; i < numConfigs; ++i) {
            m_loader.ArCameraConfigList_getItem(m_session, configList, i, tempConfig);

            int32_t w = 0, h = 0;
            m_loader.ArCameraConfig_getImageDimensions(m_session, tempConfig, &w, &h);

            // Deduplicate
            bool dup = false;
            for (auto& s : seen) {
                if (s.w == w && s.h == h) { dup = true; break; }
            }
            if (dup) continue;
            seen.push_back({w, h});

            m_resolutions.push_back({w, h});
        }

        // Sort ascending by pixel count
        std::sort(m_resolutions.begin(), m_resolutions.end(),
                  [](const CameraResolution& a, const CameraResolution& b) {
                      return (int64_t)a.width * a.height < (int64_t)b.width * b.height;
                  });

        m_loader.ArCameraConfig_destroy(tempConfig);
        m_loader.ArCameraConfigList_destroy(configList);
        m_loader.ArCameraConfigFilter_destroy(filter);

        LOGI("ARSessionManager: %zu unique resolutions available:", m_resolutions.size());
        for (size_t i = 0; i < m_resolutions.size(); ++i) {
            LOGI("  [%zu] %dx%d", i, m_resolutions[i].width, m_resolutions[i].height);
        }
    }

    bool ARSessionManager::setResolution(int32_t index) {
        if (index < 0 || index >= static_cast<int32_t>(m_resolutions.size())) {
            LOGE("ARSessionManager::setResolution: index %d out of range [0, %zu)",
                 index, m_resolutions.size());
            return false;
        }

        const auto& target = m_resolutions[index];
        LOGI("ARSessionManager::setResolution -> [%d] %dx%d", index, target.width, target.height);

        // Must release camera image before pausing
        releaseCameraImage();

        // Pause session (ArSession_setCameraConfig requires paused session)
        m_loader.ArSession_pause(m_session);

        // Find the ARCore config that matches this resolution
        ArCameraConfigFilter* filter = nullptr;
        m_loader.ArCameraConfigFilter_create(m_session, &filter);

        ArCameraConfigList* configList = nullptr;
        m_loader.ArCameraConfigList_create(m_session, &configList);

        m_loader.ArSession_getSupportedCameraConfigsWithFilter(
                m_session, filter, configList);

        int32_t numConfigs = 0;
        m_loader.ArCameraConfigList_getSize(m_session, configList, &numConfigs);

        ArCameraConfig* tempConfig = nullptr;
        m_loader.ArCameraConfig_create(m_session, &tempConfig);

        bool found = false;
        for (int32_t i = 0; i < numConfigs; ++i) {
            m_loader.ArCameraConfigList_getItem(m_session, configList, i, tempConfig);

            int32_t w = 0, h = 0;
            m_loader.ArCameraConfig_getImageDimensions(m_session, tempConfig, &w, &h);

            if (w == target.width && h == target.height) {
                ArStatus st = m_loader.ArSession_setCameraConfig(m_session, tempConfig);
                if (st == AR_SUCCESS) {
                    m_currentResolutionIndex = index;
                    found = true;
                    LOGI("ARSessionManager: camera config set to %dx%d", w, h);
                } else {
                    LOGE("ARSessionManager: ArSession_setCameraConfig failed: %d", st);
                }
                break;
            }
        }

        m_loader.ArCameraConfig_destroy(tempConfig);
        m_loader.ArCameraConfigList_destroy(configList);
        m_loader.ArCameraConfigFilter_destroy(filter);

        // Resume session
        ArStatus st = m_loader.ArSession_resume(m_session);
        if (st != AR_SUCCESS) {
            LOGE("ARSessionManager: ArSession_resume failed after config change: %d", st);
        }

        return found;
    }

    void ARSessionManager::getProjectionMatrix(float nearClip, float farClip, float* outMatrix) {
        ArCamera* camera = nullptr;
        m_loader.ArFrame_acquireCamera(m_session, m_frame, &camera);
        m_loader.ArCamera_getProjectionMatrix(m_session, camera, nearClip, farClip, outMatrix);
        m_loader.ArCamera_release(camera);
    }

    void ARSessionManager::forEachPlane(
            const std::function<void(
                    int64_t planeId,
                    const float* modelMatrix,
                    const float* polygon,
                    int polygonFloatCount
            )>& fn)
    {
        int32_t count = 0;
        m_loader.ArTrackableList_getSize(m_session, m_planeList, &count);

        for (int i = 0; i < count; ++i) {

            ArTrackable* trackable = nullptr;
            m_loader.ArTrackableList_acquireItem(
                    m_session,
                    m_planeList,
                    i,
                    &trackable
            );

            if (!trackable)
                continue;

            ArPlane* plane = ArAsPlane(trackable);

            // Tracking state
            ArTrackingState state;
            m_loader.ArTrackable_getTrackingState(
                    m_session,
                    trackable,
                    &state
            );

            if (state != AR_TRACKING_STATE_TRACKING) {
                m_loader.ArTrackable_release(trackable);
                continue;
            }

            // Subsumed planes (ignore merged planes)
            ArPlane* subsumed = nullptr;
            m_loader.ArPlane_acquireSubsumedBy(
                    m_session,
                    plane,
                    &subsumed
            );

            if (subsumed != nullptr) {
                m_loader.ArTrackable_release(ArAsTrackable(subsumed));
                m_loader.ArTrackable_release(trackable);
                continue;
            }

            // Pose → model matrix
            ArPose* pose = nullptr;
            m_loader.ArPose_create(m_session, nullptr, &pose);

            m_loader.ArPlane_getCenterPose(
                    m_session,
                    plane,
                    pose
            );

            float model[16];
            m_loader.ArPose_getMatrix(
                    m_session,
                    pose,
                    model
            );

            m_loader.ArPose_destroy(pose);

            // Polygon (XZ plane, local space)
            int32_t polySize = 0;
            m_loader.ArPlane_getPolygonSize(
                    m_session,
                    plane,
                    &polySize
            );

            if (polySize > 0) {
                std::vector<float> polygon(polySize);
                m_loader.ArPlane_getPolygon(
                        m_session,
                        plane,
                        polygon.data()
                );

                int64_t planeId = reinterpret_cast<int64_t>(trackable);
                fn(planeId, model, polygon.data(), polySize);
            }

            m_loader.ArTrackable_release(trackable);
        }
    }

}