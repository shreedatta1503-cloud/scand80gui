/****************************************************************************
 *
 * (c) 2024 QGroundControl Development Team. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QQueue>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <QtCore/QWaitCondition>
#include <QtQmlIntegration/QtQmlIntegration>

class EventLoggerWorker;

/// \brief Application-wide asynchronous, thread-safe event logger.
///
/// EventLogger is the GUI-thread facade for a dedicated logging subsystem. It is the single
/// entry point used to record timestamped operational events (payload-drop sequence, navigation
/// light state changes, …). It is intentionally trivial on the calling side:
///
///   * logEvent() is callable from ANY thread (GUI or worker). It stamps the event with an
///     ISO-8601 UTC timestamp at the moment of the call (so the recorded time reflects when the
///     event happened, not when it was eventually written), then hands the entry to a background
///     worker through a mutex-protected producer/consumer queue and returns immediately. No file
///     I/O, no formatting and no flushing ever happens on the caller's thread, so the UI thread
///     is never blocked — even under a burst of events.
///
///   * All disk work (open / format / write / flush / close) happens on a private worker QThread
///     (EventLoggerWorker) so it can run concurrently on another CPU core. Bursts of events are
///     formatted in parallel across the global thread pool via QtConcurrent.
///
/// The logger is exposed to QML as `QGroundControl.eventLogger` and to C++ via instance().
class EventLogger : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

public:
    explicit EventLogger(QObject *parent = nullptr);
    ~EventLogger() override;

    /// Application-wide singleton (mirrors SettingsManager).
    static EventLogger *instance();

    /// Resolve the log-file location (QGC user data directory) and start the worker thread.
    /// Safe to call once after SettingsManager::init(). Subsequent calls are no-ops.
    void init();

    /// Record an event. Thread-safe and non-blocking — see the class comment.
    /// The line written is: "<ISO-8601 UTC timestamp> | <event>".
    Q_INVOKABLE void logEvent(const QString &event);

    /// Absolute path of the active log file (empty until init()). Exposed for tests/diagnostics.
    Q_INVOKABLE QString logFilePath() const { return _logFilePath; }

    /// Flush any queued entries and stop the worker thread cleanly. Called from the destructor;
    /// also safe to call explicitly during a controlled application shutdown.
    void shutdown();

signals:
    /// Emitted on the GUI thread once an entry has been durably written, carrying the exact line
    /// that was persisted. Lets QML/UI mirror the file without touching disk. Optional consumer.
    void eventLogged(const QString &line);

private slots:
    void _onWorkerEntryWritten(const QString &line);

private:
    // EventLoggerWorker is itself a QThread (it overrides run()); it owns the file + queue and
    // executes its consumer loop on a separate core.
    EventLoggerWorker *_worker = nullptr;
    QString            _logFilePath;             ///< Resolved once in init().
    bool               _initialized = false;

    static constexpr const char *kLogFileName = "QGC_Event_Log.txt";
};

/// \brief Consumer worker for EventLogger. Runs its own thread (run() override) and owns the
/// authoritative producer/consumer queue.
///
/// Threading contract:
///   * enqueue() is the PRODUCER side, called from arbitrary threads. It locks _mutex, appends to
///     _queue and wakes the consumer via _wait. O(1) and lock-held-for-microseconds only.
///   * run() is the CONSUMER. It blocks on _wait until work (or a stop request) arrives, drains the
///     whole queue under the lock into a local batch, releases the lock, then formats + writes +
///     flushes the batch. Draining in batches keeps the lock contention minimal and lets a burst
///     be written in one fsync. Formatting of large bursts is parallelised with QtConcurrent so it
///     scales across cores.
///   * The QFile/QTextStream are touched ONLY on the worker thread (thread confinement), so they
///     need no locking of their own.
class EventLoggerWorker : public QThread
{
    Q_OBJECT

public:
    explicit EventLoggerWorker(const QString &filePath, QObject *parent = nullptr);

    /// PRODUCER: enqueue a pre-stamped entry. Thread-safe, non-blocking.
    void enqueue(const QString &isoTimestampUtc, const QString &event);

    /// Request a graceful stop: the consumer drains everything still queued, flushes and exits.
    void stop();

signals:
    /// Emitted (queued) after a line is written to disk, so the GUI facade can re-broadcast it.
    void entryWritten(const QString &line);

protected:
    void run() override;     ///< Consumer loop. Runs on this QThread.

private:
    struct Entry {
        QString timestamp;   ///< ISO-8601 UTC, captured by the producer at logEvent() time.
        QString event;       ///< Human-readable event text.
    };

    /// Pure formatter: "<timestamp> | <event>". Static + side-effect-free so it is safe to run on
    /// the thread pool (QtConcurrent) when formatting a burst across multiple cores.
    static QString formatLine(const Entry &entry);

    const QString  _filePath;

    QMutex         _mutex;                 ///< Guards _queue and _stopRequested.
    QWaitCondition _wait;                  ///< Consumer sleeps here until producers signal work.
    QQueue<Entry>  _queue;                 ///< Producer/consumer queue (guarded by _mutex).
    bool           _stopRequested = false; ///< Guarded by _mutex.

    /// Bursts at/above this size are formatted in parallel across the global thread pool.
    static constexpr int kParallelFormatThreshold = 16;
};
