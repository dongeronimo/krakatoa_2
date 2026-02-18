#ifndef KRAKATOA_EGL_DUMMY_CONTEXT_H
#define KRAKATOA_EGL_DUMMY_CONTEXT_H
#include <EGL/egl.h>

namespace ar {

    /**
     * Minimal EGL context that exists solely to satisfy ARCore's internal
     * requirement for an active GL context during ArSession_update().
     *
     * No rendering is done through this context. No textures are created.
     * It's a 1x1 pbuffer surface with the bare minimum ES 3.0 config.
     *
     * Usage:
     *   EglDummyContext egl;
     *   egl.initialize();          // once, at startup
     *   egl.makeCurrent();         // before arSessionManager->onDrawFrame()
     *   // ... ARCore calls ...
     *   egl.makeNonCurrent();      // optional, if you need to release
     *   egl.destroy();             // cleanup
     */
    class EglDummyContext {
    public:
        bool initialize();
        void makeCurrent();
        void makeNonCurrent();
        void destroy();

        bool isInitialized() const { return m_initialized; }

    private:
        EGLDisplay m_display = EGL_NO_DISPLAY;
        EGLContext m_context = EGL_NO_CONTEXT;
        EGLSurface m_surface = EGL_NO_SURFACE;
        EGLConfig  m_config  = nullptr;
        bool m_initialized = false;
    };

} // namespace ar
#endif //KRAKATOA_EGL_DUMMY_CONTEXT_H
