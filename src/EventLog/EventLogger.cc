/****************************************************************************
 *
 * (c) 2024 QGroundControl Development Team. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "EventLogger.h"

#include "AppSettings.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"

#include <QtConcurrent/QtConcurrent>
#include <QtCore/QApplicationStatic>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringConverter>
#include <QtCore/QTextStream>

// Logged at Info level (QGC_LOGGING_CATEGORY_ON) so every event is also visible in QGC's existing
// in-app Log Viewer / console session log, satisfying the "session logging" requirement.
QGC_LOGGING_CATEGORY_ON(EventLoggerLog, "EventLog.EventLogger")

Q_APPLICATION_STATIC(EventLogger, _eventLoggerInstance);

// ============================================================================
// EventLogger — GUI-thread facade
// ============================================================================

EventLogger::EventLogger(QObject *parent)
    : QObject(parent)
{
}

EventLogger::~EventLogger()
{
    shutdown();
}

EventLogger *EventLogger::instance()
{
    return _eventLoggerInstance();
}

void EventLogger::init()
{
    if (_initialized) {
        return;
    }

    // Resolve the persistent log location inside the QGC user data directory. Prefer the standard
    // "Logs" sub-directory (alongside telemetry/app logs); fall back to the platform app-data
    // location if the save path is not yet provisioned (e.g. very early boot / unit tests).
    QString logDir = SettingsManager::instance()->appSettings()->logSavePath();
    if (logDir.isEmpty()) {
        logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(logDir);
    _logFilePath = QDir(logDir).filePath(kLogFileName);

    // Spin up the dedicated consumer thread. The worker keeps its affinity on the GUI thread (the
    // standard QThread-subclass idiom) while its run() loop executes on the new thread. Because
    // entryWritten is emitted from run() (worker thread) and this facade lives on the GUI thread,
    // Qt delivers it via a thread-safe queued connection automatically.
    _worker = new EventLoggerWorker(_logFilePath, this);
    _worker->setObjectName(QStringLiteral("EventLoggerWorker"));
    (void) connect(_worker, &EventLoggerWorker::entryWritten,
                   this, &EventLogger::_onWorkerEntryWritten);
    _worker->start();

    _initialized = true;
    qCInfo(EventLoggerLog) << "Event logging started ->" << _logFilePath;
}

void EventLogger::logEvent(const QString &event)
{
    if (!_initialized || !_worker) {
        // Logging not yet available (or already shut down). Never crash a caller for this; still
        // surface the event to the console session log so nothing is silently lost.
        qCInfo(EventLoggerLog).noquote() << "[pre-init]" << event;
        return;
    }

    // Stamp the event NOW, on the caller's thread, so the recorded time is the event time rather
    // than the (slightly later) write time. ISO-8601 in UTC; Qt appends the trailing 'Z'.
    const QString isoUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    _worker->enqueue(isoUtc, event);
}

void EventLogger::shutdown()
{
    if (!_worker) {
        return;
    }
    // Ask the consumer to drain everything still queued, flush and exit, then block until run()
    // has fully returned. After wait() the worker thread is finished, so it is safe to delete the
    // QObject directly. Clearing _worker makes a second shutdown() (e.g. dtor after an explicit
    // call) a no-op.
    _worker->stop();
    _worker->wait();
    delete _worker;
    _worker = nullptr;
    _initialized = false;
}

void EventLogger::_onWorkerEntryWritten(const QString &line)
{
    emit eventLogged(line);
}

// ============================================================================
// EventLoggerWorker — consumer, runs entirely on its own thread
// ============================================================================

EventLoggerWorker::EventLoggerWorker(const QString &filePath, QObject *parent)
    : QThread(parent)
    , _filePath(filePath)
{
}

void EventLoggerWorker::enqueue(const QString &isoTimestampUtc, const QString &event)
{
    {
        QMutexLocker locker(&_mutex);
        _queue.enqueue(Entry{isoTimestampUtc, event});
    }
    // Wake the consumer outside the lock to minimise the time it is held.
    _wait.wakeOne();
}

void EventLoggerWorker::stop()
{
    {
        QMutexLocker locker(&_mutex);
        _stopRequested = true;
    }
    _wait.wakeOne();
}

QString EventLoggerWorker::formatLine(const Entry &entry)
{
    // Example: "2026-02-15T14:32:18.523Z | Payload Drop Confirmed"
    return entry.timestamp + QStringLiteral(" | ") + entry.event;
}

void EventLoggerWorker::run()
{
    // Open the file in append mode so logs persist across restarts and are never truncated.
    QFile file(_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qCWarning(EventLoggerLog) << "Could not open event log for writing:" << _filePath
                                  << file.errorString();
        // Even if the file cannot be opened we still drain the queue (below) so producers never
        // block and the events still reach the console session log.
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    for (;;) {
        QList<Entry> batch;
        {
            QMutexLocker locker(&_mutex);
            // Sleep until there is work or a stop has been requested. QWaitCondition::wait atomically
            // releases the mutex while sleeping and re-acquires it on wake (guards against lost
            // wake-ups / spurious wake-ups via the while-predicate).
            while (_queue.isEmpty() && !_stopRequested) {
                _wait.wait(&_mutex);
            }
            if (_queue.isEmpty() && _stopRequested) {
                break;  // Nothing left to write and shutdown requested: exit the loop.
            }
            // Drain the entire queue in one go so a burst is written (and flushed) together.
            batch.reserve(_queue.size());
            while (!_queue.isEmpty()) {
                batch.append(_queue.dequeue());
            }
        } // _mutex released here — formatting/writing happens lock-free.

        // Format the batch. For large bursts, fan the (pure, side-effect-free) formatter out across
        // the global thread pool so multiple CPU cores share the work; small batches are formatted
        // inline to avoid pool-dispatch overhead. blockingMapped preserves input ordering.
        QList<QString> lines;
        if (batch.size() >= kParallelFormatThreshold) {
            lines = QtConcurrent::blockingMapped(batch, &EventLoggerWorker::formatLine);
        } else {
            lines.reserve(batch.size());
            for (const Entry &entry : std::as_const(batch)) {
                lines.append(formatLine(entry));
            }
        }

        for (const QString &line : std::as_const(lines)) {
            if (file.isOpen()) {
                stream << line << '\n';
            }
            // Mirror into QGC's existing logging infrastructure (Log Viewer + console).
            qCInfo(EventLoggerLog).noquote() << line;
            emit entryWritten(line);
        }

        // Flush after each drained batch so a crash loses at most the in-flight burst, and write
        // the OS buffers through to disk to prevent corruption/partial lines.
        if (file.isOpen()) {
            stream.flush();
            file.flush();
        }
    }

    // Final flush + close on graceful shutdown.
    if (file.isOpen()) {
        stream.flush();
        file.flush();
        file.close();
    }
}
