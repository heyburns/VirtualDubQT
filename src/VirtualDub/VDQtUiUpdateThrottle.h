#ifndef VDQT_UI_UPDATE_THROTTLE_H
#define VDQT_UI_UPDATE_THROTTLE_H

#include <QtGlobal>
#include <algorithm>

// Limit presentation work, never media processing or cancellation callbacks.
// Callers supply elapsed milliseconds so the policy is deterministic in tests.
// Explicitly force the final update; its frame need not have the largest source
// ordinal when a timeline contains cuts, reordered ranges or masked frames.
class VDQtUiUpdateThrottle {
public:
    explicit VDQtUiUpdateThrottle(qint64 intervalMilliseconds)
        : mInterval(std::max<qint64>(1, intervalMilliseconds)) {}

    bool shouldUpdate(qint64 elapsedMilliseconds, bool force = false) {
        elapsedMilliseconds = std::max<qint64>(0, elapsedMilliseconds);
        if (force || !mInitialized || elapsedMilliseconds < mLastUpdate
            || elapsedMilliseconds - mLastUpdate >= mInterval) {
            mInitialized = true;
            mLastUpdate = elapsedMilliseconds;
            return true;
        }
        return false;
    }

private:
    const qint64 mInterval;
    qint64 mLastUpdate = 0;
    bool mInitialized = false;
};

#endif
