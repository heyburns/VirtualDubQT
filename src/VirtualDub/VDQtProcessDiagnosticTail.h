#ifndef VDQT_PROCESS_DIAGNOSTIC_TAIL_H
#define VDQT_PROCESS_DIAGNOSTIC_TAIL_H

#include <QString>
#include <QByteArray>
#include <QPointer>
#include <QProcess>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

// Owner-thread-only diagnostic capture for a QProcess. Connect before start():
// blocking waitForFinished() also emits ready-read signals, so draining those
// signals bounds QProcess's retention even without a GUI event loop. A fixed
// byte ring keeps only the latest diagnostics without allocating for a noisy
// encoder's complete stdout/stderr history. Prefer MergedChannels when the
// relative order of the two streams matters.
class VDQtProcessDiagnosticTail final {
public:
    explicit VDQtProcessDiagnosticTail(QProcess& process, qsizetype capacity = 64 * 1024)
        : mProcess(&process), mStorage(std::clamp<qsizetype>(capacity, 1, 4 * 1024 * 1024),
                                     Qt::Uninitialized) {
        mOutputConnection = QObject::connect(&process, &QProcess::readyReadStandardOutput,
            &process, [this] { drain(); }, Qt::DirectConnection);
        mErrorConnection = QObject::connect(&process, &QProcess::readyReadStandardError,
            &process, [this] { drain(); }, Qt::DirectConnection);
    }
    ~VDQtProcessDiagnosticTail() {
        // The helper may be destroyed before the QProcess. Never leave a
        // capture of this attached to signals emitted during process teardown.
        QObject::disconnect(mOutputConnection);
        QObject::disconnect(mErrorConnection);
    }
    VDQtProcessDiagnosticTail(const VDQtProcessDiagnosticTail&) = delete;
    VDQtProcessDiagnosticTail& operator=(const VDQtProcessDiagnosticTail&) = delete;

    void drain() {
        // This owner-thread drain does not pump events. Keep one checked
        // snapshot rather than repeatedly converting the guarded pointer.
        QProcess *const process = mProcess.data();
        if (!process || mDraining) return;
        mDraining = true;
        const auto previousChannel = process->readChannel();
        std::array<char, 4096> block;
        for (const auto channel : {QProcess::StandardOutput, QProcess::StandardError}) {
            process->setReadChannel(channel);
            while (process->bytesAvailable() > 0) {
                const qint64 amount = process->read(block.data(), block.size());
                if (amount <= 0) break;
                append(block.data(), static_cast<qsizetype>(amount));
                mTotalBytes = amount > std::numeric_limits<qint64>::max() - mTotalBytes
                    ? std::numeric_limits<qint64>::max() : mTotalBytes + amount;
            }
        }
        process->setReadChannel(previousChannel);
        mDraining = false;
    }

    QByteArray bytes() const {
        QByteArray result(mSize, Qt::Uninitialized);
        const qsizetype start = (mWritePosition + mStorage.size() - mSize) % mStorage.size();
        const qsizetype first = std::min(mSize, mStorage.size() - start);
        if (first) std::memcpy(result.data(), mStorage.constData() + start, first);
        if (first < mSize) std::memcpy(result.data() + first, mStorage.constData(), mSize - first);
        return result;
    }
    qsizetype retainedBytes() const { return mSize; }
    qsizetype capacity() const { return mStorage.size(); }
    qint64 totalBytesDrained() const { return mTotalBytes; }

private:
    void append(const char *data, qsizetype size) {
        if (size >= mStorage.size()) {
            std::memcpy(mStorage.data(), data + size - mStorage.size(), mStorage.size());
            mWritePosition = 0;
            mSize = mStorage.size();
            return;
        }
        const qsizetype first = std::min(size, mStorage.size() - mWritePosition);
        std::memcpy(mStorage.data() + mWritePosition, data, first);
        if (first < size) std::memcpy(mStorage.data(), data + first, size - first);
        mWritePosition = (mWritePosition + size) % mStorage.size();
        mSize = std::min(mStorage.size(), mSize + size);
    }

    QPointer<QProcess> mProcess;
    QMetaObject::Connection mOutputConnection;
    QMetaObject::Connection mErrorConnection;
    QByteArray mStorage;
    qsizetype mSize = 0;
    qsizetype mWritePosition = 0;
    qint64 mTotalBytes = 0;
    bool mDraining = false;
};

#endif
