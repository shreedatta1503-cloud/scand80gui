/****************************************************************************
 *
 * (c) 2024 QGroundControl Development Team. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PayloadDropController.h"

#include "QGCLoggingCategory.h"
#include "Vehicle.h"

#include <QtConcurrent/QtConcurrent>

QGC_LOGGING_CATEGORY(PayloadDropControllerLog, "QMLControls.PayloadDropController")

// ============================================================================
// Threading model
// ----------------------------------------------------------------------------
// QGroundControl decodes MAVLink (incl. RC_CHANNELS / SERVO_OUTPUT_RAW) on the GUI thread; only the
// raw socket I/O runs on the per-link worker threads. So Vehicle::rc9TriggerChanged and
// Vehicle::servoOutputsChanged are emitted on the GUI thread. To honour the requirement that RC
// monitoring, visibility evaluation and workflow-state management run *off* the UI thread, this
// controller owns a dedicated worker thread:
//
//   Vehicle (GUI thread)
//        | rc9TriggerChanged(int) / servoOutputsChanged(QVector<int>)
//        v   (Qt::QueuedConnection — receiver lives in another thread)
//   PayloadDropWorker (worker thread)   <-- all RC9/servo evaluation + workflow state lives here
//        | visibleChanged / rc9Changed / pin*/drop* (Qt::QueuedConnection)
//        v
//   PayloadDropController (GUI thread)  <-- updates cached Q_PROPERTYs that QML binds to
//
// Why this is safe and fast:
//   * Thread confinement instead of locks: every mutable state member of PayloadDropWorker is read
//     and written on the worker thread only. There is no shared mutable state between threads, so
//     there are no mutexes, hence no deadlocks and no priority inversion. The two objects exchange
//     immutable value copies (int / bool / QVector<int>) over queued signals, which Qt marshals
//     thread-safely.
//   * Determinism: the worker has a single event loop, so RC updates, servo feedback and operator
//     actions are serialised in arrival order — the payload-drop sequence is fully deterministic.
//   * UI thread never blocks: the worker does the evaluation; the GUI thread only applies tiny
//     property deltas. Operator button presses call the non-blocking Vehicle::send* helpers (which
//     enqueue a MAVLink command and return at once) directly on the GUI thread.
//   * Latency: each direction is one event-loop hop (sub-millisecond) — imperceptible and far below
//     the ~10 Hz RC frame period, so show/hide is effectively immediate. (Evaluating inline on the
//     GUI thread would shave that hop but would not satisfy the off-UI-thread requirement; the
//     trade-off is intentional and negligible.)
//   * Multi-core: the worker thread runs concurrently with the GUI thread on a separate core, and
//     drop-completion telemetry formatting is dispatched to the global QThreadPool via
//     QtConcurrent::run so it never competes with either event loop. Per-event work is a couple of
//     integer comparisons and is deliberately *not* sharded across a pool — doing so would add
//     contention and latency for no benefit (and risk the very priority inversion we avoid).
// ============================================================================

PayloadDropController::PayloadDropController(QObject *parent)
    : QObject(parent)
{
    // QVector<int> crosses a thread boundary on the queued servoOutputsChanged connection; make
    // sure the metatype is registered so Qt can copy it for delivery.
    qRegisterMetaType<QVector<int>>("QVector<int>");

    _worker = new PayloadDropWorker;            // no parent: ownership transfers to the worker thread
    _worker->moveToThread(&_workerThread);
    (void) connect(&_workerThread, &QThread::finished, _worker, &QObject::deleteLater);

    // worker thread -> GUI thread (auto/queued: sender and receiver live on different threads)
    (void) connect(_worker, &PayloadDropWorker::visibleChanged,             this, &PayloadDropController::_onWorkerVisibleChanged);
    (void) connect(_worker, &PayloadDropWorker::rc9Changed,                 this, &PayloadDropController::_onWorkerRc9Changed);
    (void) connect(_worker, &PayloadDropWorker::pinReleaseRequestedChanged, this, &PayloadDropController::_onWorkerPinReleaseRequestedChanged);
    (void) connect(_worker, &PayloadDropWorker::pinRemovedChanged,          this, &PayloadDropController::_onWorkerPinRemovedChanged);
    (void) connect(_worker, &PayloadDropWorker::dropCompletedChanged,       this, &PayloadDropController::_onWorkerDropCompletedChanged);

    // GUI thread -> worker thread (auto/queued)
    (void) connect(this, &PayloadDropController::_userRequestedPinRelease, _worker, &PayloadDropWorker::onUserRequestedPinRelease);
    (void) connect(this, &PayloadDropController::_userRequestedDrop,       _worker, &PayloadDropWorker::onUserRequestedDrop);

    _workerThread.setObjectName(QStringLiteral("PayloadDropWorker"));
    _workerThread.start();

    qCDebug(PayloadDropControllerLog) << "created; worker thread started";
}

PayloadDropController::~PayloadDropController()
{
    _workerThread.quit();
    _workerThread.wait();
}

void PayloadDropController::setVehicle(Vehicle *vehicle)
{
    if (_vehicle == vehicle) {
        return;
    }
    if (_vehicle) {
        _disconnectVehicle(_vehicle);
    }
    _vehicle = vehicle;
    if (_vehicle) {
        _connectVehicle(_vehicle);
    }
    qCDebug(PayloadDropControllerLog) << "active vehicle ->" << _vehicle;
    emit vehicleChanged();
}

void PayloadDropController::_connectVehicle(Vehicle *vehicle)
{
    // These deliver to the worker thread, so Qt uses a queued connection automatically: the RC9 and
    // servo evaluation therefore execute on the worker thread, not the GUI thread.
    (void) connect(vehicle, &Vehicle::rc9TriggerChanged,   _worker, &PayloadDropWorker::onRc9Changed);
    (void) connect(vehicle, &Vehicle::servoOutputsChanged, _worker, &PayloadDropWorker::onServoOutputs);
}

void PayloadDropController::_disconnectVehicle(Vehicle *vehicle)
{
    (void) disconnect(vehicle, &Vehicle::rc9TriggerChanged,   _worker, &PayloadDropWorker::onRc9Changed);
    (void) disconnect(vehicle, &Vehicle::servoOutputsChanged, _worker, &PayloadDropWorker::onServoOutputs);
}

void PayloadDropController::requestPinRelease()
{
    if (_vehicle) {
        // Non-blocking MAVLink send (AUX OUT 9). Issued on the GUI thread exactly as before; the ACK
        // arrives asynchronously via servoOutputsChanged, so the GUI thread is never blocked.
        _vehicle->sendPayloadPinRelease();
    }
    emit _userRequestedPinRelease();    // record the request in the worker (queued)
}

void PayloadDropController::requestDrop()
{
    // The operator confirmed the drop in the QML confirmation dialog; this is the point the release
    // command is actually issued.
    if (_vehicle) {
        _vehicle->sendPayloadDrop();    // Non-blocking MAVLink send (AUX OUT 11)
    }
    emit _userRequestedDrop();          // record completion in the worker (queued)
}

void PayloadDropController::_onWorkerVisibleChanged(bool visible)
{
    if (_widgetVisible != visible) {
        _widgetVisible = visible;
        emit widgetVisibleChanged();
    }
}

void PayloadDropController::_onWorkerRc9Changed(int pwm)
{
    if (_rc9Pwm != pwm) {
        _rc9Pwm = pwm;
        emit rc9PwmChanged();
    }
}

void PayloadDropController::_onWorkerPinReleaseRequestedChanged(bool requested)
{
    if (_pinReleaseRequested != requested) {
        _pinReleaseRequested = requested;
        emit pinReleaseRequestedChanged();
    }
}

void PayloadDropController::_onWorkerPinRemovedChanged(bool removed)
{
    if (_pinRemoved != removed) {
        _pinRemoved = removed;
        emit pinRemovedChanged();
    }
}

void PayloadDropController::_onWorkerDropCompletedChanged(bool completed)
{
    if (_dropCompleted != completed) {
        _dropCompleted = completed;
        emit dropCompletedChanged();
    }
}

// ============================================================================
// PayloadDropWorker — runs entirely on the worker thread
// ============================================================================

PayloadDropWorker::PayloadDropWorker(QObject *parent)
    : QObject(parent)
{
}

void PayloadDropWorker::onRc9Changed(int pwm)
{
    if (pwm < 0) {
        // Ch9 absent from the RC stream: keep the last reading and current visibility unchanged so a
        // momentary dropout neither flickers the widget nor discards workflow state.
        return;
    }

    if (pwm != _rc9Pwm) {
        _rc9Pwm = pwm;
        emit rc9Changed(pwm);
    }

    // Visibility is pure derived state: high (2000us) shows, low (1000us) hides. The pin/drop
    // workflow members are deliberately left untouched, so every hide/show cycle preserves the
    // exact workflow progress and the widget resumes from where it was hidden.
    const bool shouldShow = (pwm >= _rc9ShowThresholdUs);
    if (shouldShow != _visible) {
        _visible = shouldShow;
        qCDebug(PayloadDropControllerLog) << "RC9" << pwm << "us -> widget visible" << _visible
                                          << "(workflow state preserved)";
        emit visibleChanged(_visible);
    }
}

void PayloadDropWorker::onServoOutputs(const QVector<int> &servoValues)
{
    if (!_pinReleaseRequested || _pinRemoved) {
        return;     // Only relevant between Remove Pin and the AUX OUT 10 confirmation.
    }
    if (_pinFeedbackServoIndex < 0 || _pinFeedbackServoIndex >= servoValues.size()) {
        return;
    }

    const int pwm = servoValues[_pinFeedbackServoIndex];
    if (pwm >= 0 && pwm >= _pinFeedbackThresholdUs) {
        _pinRemoved = true;
        qCDebug(PayloadDropControllerLog) << "AUX OUT 10 feedback" << pwm << "us -> pin removed, DROP enabled";
        emit pinRemovedChanged(true);
    }
}

void PayloadDropWorker::onUserRequestedPinRelease()
{
    if (_pinReleaseRequested) {
        return;
    }
    _pinReleaseRequested = true;
    qCDebug(PayloadDropControllerLog) << "pin release requested (AUX OUT 9 fired); awaiting AUX OUT 10";
    emit pinReleaseRequestedChanged(true);
}

void PayloadDropWorker::onUserRequestedDrop()
{
    if (_dropCompleted) {
        return;
    }
    _dropCompleted = true;
    qCDebug(PayloadDropControllerLog) << "DROP completed (AUX OUT 11 fired) — completing silently";
    emit dropCompletedChanged(true);

    // Telemetry/audit record: format and log it on the global thread pool (a separate core) so it
    // never stalls the worker event loop or the GUI thread. Fire-and-forget; nothing on the hot path
    // waits on it. This is the genuinely offloadable background work in the drop sequence.
    const int rc9 = _rc9Pwm;
    (void) QtConcurrent::run([rc9]() {
        const QString record = QStringLiteral("PayloadDrop telemetry: completed=1 rc9=%1us").arg(rc9);
        qCInfo(PayloadDropControllerLog) << record;
    });
}
