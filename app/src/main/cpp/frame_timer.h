#pragma once
#include <chrono>

namespace graphics {
    /// Frame timer that tracks delta time and total elapsed time.
    /// Similar to Unity's Time class - provides deltaTime and time fields
    /// for use in animations and frame-rate-independent logic.
    /// Supports pausing: when paused, deltaTime is 0 and totalTime stops advancing.
    class FrameTimer {
    public:
        FrameTimer();

        /// Call once at the start of each frame to update timing.
        void Tick();

        /// Pause the timer. While paused, deltaTime returns 0 and totalTime freezes.
        void Pause();

        /// Resume the timer after a pause. Avoids a large delta spike on the first
        /// frame after resuming.
        void Resume();

        /// Time in seconds since the last frame (0 on first frame and while paused).
        float GetDeltaTime() const { return mDeltaTime; }

        /// Total unpaused time in seconds since the timer started.
        float GetTotalTime() const { return mTotalTime; }

        /// Whether the timer is currently paused.
        bool IsPaused() const { return mPaused; }

    private:
        using Clock = std::chrono::high_resolution_clock;
        using TimePoint = Clock::time_point;

        TimePoint mLastFrameTime;
        float mDeltaTime;
        float mTotalTime;
        bool mPaused;
        bool mFirstFrame;
    };
}
