#include "frame_timer.h"

namespace graphics {
    FrameTimer::FrameTimer()
        : mLastFrameTime(Clock::now()),
          mDeltaTime(0.0f),
          mTotalTime(0.0f),
          mPaused(false),
          mFirstFrame(true) {
    }

    void FrameTimer::Tick() {
        TimePoint now = Clock::now();

        if (mFirstFrame) {
            mDeltaTime = 0.0f;
            mFirstFrame = false;
        } else if (mPaused) {
            mDeltaTime = 0.0f;
        } else {
            std::chrono::duration<float> elapsed = now - mLastFrameTime;
            mDeltaTime = elapsed.count();
            mTotalTime += mDeltaTime;
        }

        mLastFrameTime = now;
    }

    void FrameTimer::Pause() {
        mPaused = true;
    }

    void FrameTimer::Resume() {
        if (mPaused) {
            mPaused = false;
            // Reset the last frame time so the first frame after resume
            // doesn't include the paused duration as a huge delta.
            mLastFrameTime = Clock::now();
        }
    }
}
