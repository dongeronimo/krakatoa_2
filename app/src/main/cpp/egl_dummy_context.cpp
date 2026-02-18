#include "egl_dummy_context.h"
#include "android_log.h"

namespace ar {

    bool EglDummyContext::initialize() {
        if (m_initialized) return true;

        // ── Display ──
        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (m_display == EGL_NO_DISPLAY) {
            LOGE("EglDummyContext: eglGetDisplay failed");
            return false;
        }

        EGLint major, minor;
        if (!eglInitialize(m_display, &major, &minor)) {
            LOGE("EglDummyContext: eglInitialize failed: 0x%x", eglGetError());
            return false;
        }
        LOGI("EglDummyContext: EGL %d.%d", major, minor);

        // ── Config ── minimal: just need a pbuffer-capable ES 3.0 config
        const EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
                EGL_RED_SIZE,        1,
                EGL_GREEN_SIZE,      1,
                EGL_BLUE_SIZE,       1,
                EGL_NONE
        };

        EGLint numConfigs = 0;
        if (!eglChooseConfig(m_display, configAttribs, &m_config, 1, &numConfigs) || numConfigs == 0) {
            LOGE("EglDummyContext: eglChooseConfig failed: 0x%x", eglGetError());
            return false;
        }

        // ── Context ── ES 3.0
        const EGLint contextAttribs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 3,
                EGL_NONE
        };

        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttribs);
        if (m_context == EGL_NO_CONTEXT) {
            LOGE("EglDummyContext: eglCreateContext failed: 0x%x", eglGetError());
            return false;
        }

        // ── Surface ── 1x1 pbuffer, never rendered to
        const EGLint surfaceAttribs[] = {
                EGL_WIDTH,  1,
                EGL_HEIGHT, 1,
                EGL_NONE
        };

        m_surface = eglCreatePbufferSurface(m_display, m_config, surfaceAttribs);
        if (m_surface == EGL_NO_SURFACE) {
            LOGE("EglDummyContext: eglCreatePbufferSurface failed: 0x%x", eglGetError());
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
            return false;
        }

        m_initialized = true;
        LOGI("EglDummyContext: initialized (1x1 pbuffer, ES 3.0) - exists only for ARCore");
        return true;
    }

    void EglDummyContext::makeCurrent() {
        if (!m_initialized) return;
        if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
            LOGE("EglDummyContext: eglMakeCurrent failed: 0x%x", eglGetError());
        }
    }

    void EglDummyContext::makeNonCurrent() {
        if (!m_initialized) return;
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    void EglDummyContext::destroy() {
        if (!m_initialized) return;

        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (m_surface != EGL_NO_SURFACE) {
            eglDestroySurface(m_display, m_surface);
            m_surface = EGL_NO_SURFACE;
        }
        if (m_context != EGL_NO_CONTEXT) {
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
        }
        if (m_display != EGL_NO_DISPLAY) {
            eglTerminate(m_display);
            m_display = EGL_NO_DISPLAY;
        }

        m_initialized = false;
        LOGI("EglDummyContext: destroyed");
    }

} // namespace ar