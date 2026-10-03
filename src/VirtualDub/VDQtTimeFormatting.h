#ifndef VDQTTIMEFORMATTING_H
#define VDQTTIMEFORMATTING_H

#include "VDQtTimingMath.h"
#include <QString>

// Share the status/jump-dialog clock format without converting total seconds
// or milliseconds to int first. Very-low-rate clips can exceed those domains
// while their individual hour/minute/second components remain representable.
inline QString VDQtFormatTimeSeconds(long double seconds, bool alwaysHours = false) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const long double ticks = std::round(seconds * 1000);
    qint64 hours = 0;
    if (!VDQtCheckedRoundedNonnegative(std::floor(ticks / 3600000), &hours))
        return QStringLiteral("Time exceeds the supported display range");
    const int minutes = static_cast<int>(std::floor(std::fmod(ticks, 3600000) / 60000));
    const int wholeSeconds = static_cast<int>(std::floor(std::fmod(ticks, 60000) / 1000));
    const int milliseconds = static_cast<int>(std::fmod(ticks, 1000));
    if (alwaysHours || hours > 0) {
        return QStringLiteral("%1:%2:%3.%4")
            .arg(hours, alwaysHours ? 2 : 0, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(wholeSeconds, 2, 10, QLatin1Char('0'))
            .arg(milliseconds, 3, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(wholeSeconds, 2, 10, QLatin1Char('0'))
        .arg(milliseconds, 3, 10, QLatin1Char('0'));
}

#endif
