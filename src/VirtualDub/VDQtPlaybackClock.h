#ifndef VDQTPLAYBACKCLOCK_H
#define VDQTPLAYBACKCLOCK_H

#include <algorithm>
#include <cmath>

// A sink may stop presenting samples at EOF, failure or an underrun while the
// video still has frames. Switch to monotonic time anchored at the last heard
// position, never the much later decode-ahead cursor or an unrelated wall time.
// Once detached, remain on the fallback clock until the next play/seek reset;
// adopting a lagging recovered sink would freeze the picture again.
class VDQtPlaybackClock {
public:
    void reset() { *this = {}; }
    double elapsed(double wallSeconds, double audioElapsed = -1) {
        wallSeconds = std::isfinite(wallSeconds) ? std::max(0.0, wallSeconds) : mLastWall;
        wallSeconds = std::max(wallSeconds, mLastWall);
        mLastWall = wallSeconds;
        const bool validAudio = std::isfinite(audioElapsed) && audioElapsed >= 0;
        if (!mFallback && validAudio) {
            if (!mUsingAudio || audioElapsed > mLastAudio + 1e-6) {
                mLastAdvanceWall = wallSeconds;
                mLastAudio = audioElapsed;
            }
            mUsingAudio = true;
            if (wallSeconds - mLastAdvanceWall <= 0.25) {
                mLast = std::max(mLast, audioElapsed);
                return mLast;
            }
        }
        if (mUsingAudio) {
            mOffset = mLast - wallSeconds;
            mUsingAudio = false;
            mFallback = true;
        }
        mLast = std::max(mLast, wallSeconds + mOffset);
        return mLast;
    }
private:
    double mLast = 0;
    double mLastWall = 0;
    double mLastAudio = 0;
    double mLastAdvanceWall = 0;
    double mOffset = 0;
    bool mUsingAudio = false;
    bool mFallback = false;
};

#endif
