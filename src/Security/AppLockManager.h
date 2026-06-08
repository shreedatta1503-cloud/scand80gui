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
#include <QtCore/QString>
#include <QtQmlIntegration/QtQmlIntegration>

/// \brief Application-level authentication / lock.
///
/// Gates access to the QGC main window behind a password at every launch. The application starts
/// LOCKED (`unlocked == false`); QML overlays a modal login screen until tryUnlock() succeeds.
///
/// Security model:
///   * The plain-text password is NEVER stored and NEVER compared directly. On first run the
///     default password ("1234") is hashed with a random per-install salt and persisted; only the
///     salted hash + salt are kept (in SecuritySettings / QSettings), so the credential survives
///     restarts but cannot be recovered from storage.
///   * Hashing uses Qt's QCryptographicHash (SHA-256) with key-stretching (repeated hashing) over
///     salt+password. Verification re-derives the hash from the entered password and compares.
///
/// Exposed to QML as `QGroundControl.appLockManager`.
class AppLockManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    /// True once the operator has authenticated this session. The QML login overlay is shown while
    /// this is false and hides itself when it becomes true.
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY unlockedChanged)

public:
    explicit AppLockManager(QObject *parent = nullptr);

    static AppLockManager *instance();

    /// Seed the default password on first run (if none is stored yet). Call once after
    /// SettingsManager::init().
    void init();

    bool unlocked() const { return _unlocked; }

    /// Attempt to unlock with @a password. Returns true and sets unlocked() on success.
    /// On failure unlocked() stays false so the login dialog remains active.
    Q_INVOKABLE bool tryUnlock(const QString &password);

    /// Re-lock the application (returns to the login screen). Provided for completeness.
    Q_INVOKABLE void lock();

    /// Change the stored password. Returns an empty string on success, otherwise a human-readable
    /// error message suitable for display:
    ///   * "Current password is incorrect"            — @a currentPassword does not match.
    ///   * "New password cannot be empty"             — @a newPassword is empty.
    ///   * "New password and confirmation do not match"— @a newPassword != @a confirmPassword.
    Q_INVOKABLE QString changePassword(const QString &currentPassword,
                                       const QString &newPassword,
                                       const QString &confirmPassword);

signals:
    void unlockedChanged();

private:
    /// Derive the hex hash of @a password using @a saltHex (key-stretched SHA-256).
    static QString _deriveHash(const QString &password, const QString &saltHex);
    /// Generate a new random salt as a hex string.
    static QString _generateSaltHex();
    /// Persist a brand-new salt+hash for @a password.
    void _storePassword(const QString &password);
    /// Constant-shape verification of @a password against the stored salt+hash.
    bool _verify(const QString &password) const;

    bool _unlocked = false;

    /// Number of SHA-256 iterations for key stretching. A fixed cost that slows brute-forcing the
    /// stored hash while remaining imperceptible for a single interactive login.
    static constexpr int kHashIterations = 100000;
    /// Default password seeded on first run, per spec.
    static constexpr const char *kDefaultPassword = "1234";
};
