/****************************************************************************
 *
 * (c) 2024 QGroundControl Development Team. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtCore/QVector>
#include <QtQmlIntegration/QtQmlIntegration>

// Vehicle.h (not just a forward declaration): the QML_ELEMENT type registration
// generated for this controller needs Vehicle to be a complete, QObject-derived
// type to register the `Q_PROPERTY(Vehicle *vehicle)` and QPointer<Vehicle> cast.
#include "Vehicle.h"

class PayloadDropWorker;

/// Backend state-manager + RC-channel monitor for the Fly View Payload Drop widget.
///
/// This object intentionally separates *workflow state* from *widget presentation*:
///
///  - The authoritative payload-drop workflow state (RC9 reading, whether the pin release was
///    requested, whether the pin is confirmed removed, whether DROP completed) lives here in C++
///    and is owned by a dedicated worker object (PayloadDropWorker) running on its own QThread.
///    Because the state is *thread-confined* to the worker, it survives every show/hide cycle of
///    the QML widget untouched — visibility is just one more piece of derived state, never a reset.
///
///  - The QML widget (PayloadDropWidget.qml) is a thin presentation layer that binds read-only to
///    the Q_PROPERTYs below and calls requestPinRelease()/requestDrop(). It never owns workflow
///    state, so hiding it (visible == false) cannot interrupt or discard the in-flight workflow.
///
/// Threading model (see PayloadDropController.cc for the full description):
///   Vehicle (GUI thread) --queued--> PayloadDropWorker (worker thread): RC9 / servo evaluation runs
///   off the UI thread.  PayloadDropWorker --queued--> PayloadDropController (GUI thread): the cached
///   Q_PROPERTYs that QML binds to are only ever mutated on the GUI thread.  All cross-thread
///   communication is via Qt's thread-safe queued signals/slots; state is lock-free by confinement.
class PayloadDropController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The vehicle whose RC9 / servo feedback drives the widget. Bound from QML to
    /// QGroundControl.multiVehicleManager.activeVehicle (may be null between vehicles).
    Q_PROPERTY(Vehicle *vehicle             READ vehicle             WRITE setVehicle NOTIFY vehicleChanged)

    /// True only while RC Channel 9 is in its "high" (2000us) position. Drives the widget's
    /// `visible`. Toggling this never alters the workflow properties below.
    Q_PROPERTY(bool     widgetVisible       READ widgetVisible       NOTIFY widgetVisibleChanged)

    /// Last observed RC Channel 9 PWM in microseconds (-1 == not seen / absent from the stream).
    Q_PROPERTY(int      rc9Pwm              READ rc9Pwm              NOTIFY rc9PwmChanged)

    /// Remove Pin pressed; AUX OUT 9 fired, awaiting AUX OUT 10 feedback.
    Q_PROPERTY(bool     pinReleaseRequested READ pinReleaseRequested NOTIFY pinReleaseRequestedChanged)

    /// AUX OUT 10 feedback confirmed the pin is out; DROP is now enabled.
    Q_PROPERTY(bool     pinRemoved          READ pinRemoved          NOTIFY pinRemovedChanged)

    /// DROP command sent (AUX OUT 11). Completes silently — there is no completion dialog.
    Q_PROPERTY(bool     dropCompleted       READ dropCompleted       NOTIFY dropCompletedChanged)

public:
    explicit PayloadDropController(QObject *parent = nullptr);
    ~PayloadDropController() override;

    Vehicle *vehicle() const { return _vehicle; }
    void setVehicle(Vehicle *vehicle);

    bool widgetVisible()       const { return _widgetVisible; }
    int  rc9Pwm()              const { return _rc9Pwm; }
    bool pinReleaseRequested() const { return _pinReleaseRequested; }
    bool pinRemoved()          const { return _pinRemoved; }
    bool dropCompleted()       const { return _dropCompleted; }

    /// Operator pressed Remove Pin. Fires AUX OUT 9 immediately (non-blocking MAVLink send on the
    /// GUI thread) and records the request in the worker. No confirmation dialog.
    Q_INVOKABLE void requestPinRelease();

    /// Operator pressed DROP. Fires AUX OUT 11 immediately and records completion in the worker.
    /// Completes silently — no "Payload Dropped" dialog/toast/popup is ever raised.
    Q_INVOKABLE void requestDrop();

signals:
    void vehicleChanged();
    void widgetVisibleChanged();
    void rc9PwmChanged();
    void pinReleaseRequestedChanged();
    void pinRemovedChanged();
    void dropCompletedChanged();

    // GUI thread -> worker thread (queued). Private transport signals, not for QML.
    void _userRequestedPinRelease();
    void _userRequestedDrop();

private slots:
    // worker thread -> GUI thread (queued). These run on the GUI thread and are the *only* place
    // the QML-visible cached state is mutated.
    void _onWorkerVisibleChanged(bool visible);
    void _onWorkerRc9Changed(int pwm);
    void _onWorkerPinReleaseRequestedChanged(bool requested);
    void _onWorkerPinRemovedChanged(bool removed);
    void _onWorkerDropCompletedChanged(bool completed);

private:
    void _connectVehicle(Vehicle *vehicle);
    void _disconnectVehicle(Vehicle *vehicle);

    QPointer<Vehicle>  _vehicle;
    QThread            _workerThread;
    PayloadDropWorker *_worker = nullptr;

    // GUI-thread cached mirror of the worker's authoritative state (read by QML bindings).
    bool _widgetVisible       = false;
    int  _rc9Pwm              = -1;
    bool _pinReleaseRequested = false;
    bool _pinRemoved          = false;
    bool _dropCompleted       = false;
};

/// Worker object that owns the authoritative payload-drop state and performs all RC9 / servo
/// evaluation. It lives on PayloadDropController's worker QThread; every slot below executes on
/// that thread, so the state members are touched by exactly one thread (thread confinement) and
/// need no locks. Results are published to the GUI thread via the queued signals.
class PayloadDropWorker : public QObject
{
    Q_OBJECT

public:
    explicit PayloadDropWorker(QObject *parent = nullptr);

public slots:
    /// RC Channel 9 PWM update (queued from Vehicle::rc9TriggerChanged). Evaluates visibility:
    /// high (>= _rc9ShowThresholdUs, i.e. 2000us) shows, low (< threshold, i.e. 1000us) hides.
    /// Never mutates the pin/drop workflow members — show/hide is non-destructive.
    void onRc9Changed(int pwm);

    /// SERVO_OUTPUT_RAW update (queued from Vehicle::servoOutputsChanged). When a pin release is
    /// pending, watches AUX OUT 10 feedback and latches _pinRemoved once it crosses threshold.
    void onServoOutputs(const QVector<int> &servoValues);

    /// Operator actions, forwarded from the GUI thread (queued).
    void onUserRequestedPinRelease();
    void onUserRequestedDrop();

signals:
    void visibleChanged(bool visible);
    void rc9Changed(int pwm);
    void pinReleaseRequestedChanged(bool requested);
    void pinRemovedChanged(bool removed);
    void dropCompletedChanged(bool completed);

private:
    // ---- Configuration (channels 1-based; ArduPilot AUX OUT n == SERVOn) ----
    const int _rc9ShowThresholdUs    = 1500;  ///< RC9 >= this (2000us) shows; below (1000us) hides
    const int _pinFeedbackServoIndex = 9;     ///< 0-based: AUX OUT 10 / SERVO10 == index 9
    const int _pinFeedbackThresholdUs= 1500;  ///< AUX OUT 10 PWM at/above which the pin is "removed"

    // ---- Authoritative state, confined to the worker thread ----
    int  _rc9Pwm              = -1;
    bool _visible             = false;
    bool _pinReleaseRequested = false;
    bool _pinRemoved          = false;
    bool _dropCompleted       = false;
};
