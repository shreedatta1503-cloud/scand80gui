/****************************************************************************
 *
 * (c) 2024 QGroundControl Development Team. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AppLockManager.h"

#include "QGCLoggingCategory.h"
#include "SecuritySettings.h"
#include "SettingsFact.h"
#include "SettingsManager.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>

QGC_LOGGING_CATEGORY(AppLockManagerLog, "Security.AppLockManager")

Q_APPLICATION_STATIC(AppLockManager, _appLockManagerInstance);

AppLockManager::AppLockManager(QObject *parent)
    : QObject(parent)
{
}

AppLockManager *AppLockManager::instance()
{
    return _appLockManagerInstance();
}

void AppLockManager::init()
{
    SecuritySettings *const settings = SettingsManager::instance()->securitySettings();

    // First run (or settings reset): no credential stored yet → seed the default password. Only the
    // salt + salted hash are written; the plain text is discarded immediately.
    if (settings->passwordHash()->rawValue().toString().isEmpty()) {
        _storePassword(QString::fromLatin1(kDefaultPassword));
        qCDebug(AppLockManagerLog) << "Seeded default application-lock password";
    }
}

bool AppLockManager::tryUnlock(const QString &password)
{
    const bool ok = _verify(password);
    if (ok) {
        if (!_unlocked) {
            _unlocked = true;
            emit unlockedChanged();
        }
        qCDebug(AppLockManagerLog) << "Application unlocked";
    } else {
        qCDebug(AppLockManagerLog) << "Failed unlock attempt (invalid password)";
    }
    return ok;
}

void AppLockManager::lock()
{
    if (_unlocked) {
        _unlocked = false;
        emit unlockedChanged();
        qCDebug(AppLockManagerLog) << "Application re-locked";
    }
}

QString AppLockManager::changePassword(const QString &currentPassword,
                                       const QString &newPassword,
                                       const QString &confirmPassword)
{
    if (!_verify(currentPassword)) {
        return tr("Current password is incorrect");
    }
    if (newPassword.isEmpty()) {
        return tr("New password cannot be empty");
    }
    if (newPassword != confirmPassword) {
        return tr("New password and confirmation do not match");
    }

    _storePassword(newPassword);
    qCDebug(AppLockManagerLog) << "Application-lock password changed";
    return QString();   // empty == success
}

QString AppLockManager::_deriveHash(const QString &password, const QString &saltHex)
{
    const QByteArray salt = QByteArray::fromHex(saltHex.toUtf8());

    // Key stretching: hash salt+password, then repeatedly re-hash salt+digest. SHA-256 throughout.
    QByteArray digest = QCryptographicHash::hash(salt + password.toUtf8(), QCryptographicHash::Sha256);
    for (int i = 1; i < kHashIterations; ++i) {
        digest = QCryptographicHash::hash(salt + digest, QCryptographicHash::Sha256);
    }
    return QString::fromLatin1(digest.toHex());
}

QString AppLockManager::_generateSaltHex()
{
    // 16 cryptographically-strong random bytes from the system CSPRNG.
    quint32 buffer[4];
    QRandomGenerator::system()->fillRange(buffer);
    const QByteArray salt(reinterpret_cast<const char *>(buffer), sizeof(buffer));
    return QString::fromLatin1(salt.toHex());
}

void AppLockManager::_storePassword(const QString &password)
{
    const QString saltHex = _generateSaltHex();
    const QString hashHex = _deriveHash(password, saltHex);

    SecuritySettings *const settings = SettingsManager::instance()->securitySettings();
    settings->passwordSalt()->setRawValue(saltHex);
    settings->passwordHash()->setRawValue(hashHex);
}

bool AppLockManager::_verify(const QString &password) const
{
    SecuritySettings *const settings = SettingsManager::instance()->securitySettings();
    const QString saltHex    = settings->passwordSalt()->rawValue().toString();
    const QString storedHash = settings->passwordHash()->rawValue().toString();
    if (saltHex.isEmpty() || storedHash.isEmpty()) {
        return false;
    }

    const QString candidate = _deriveHash(password, saltHex);

    // Length-independent, constant-time comparison so verification time does not leak how many
    // leading characters matched.
    if (candidate.size() != storedHash.size()) {
        return false;
    }
    int diff = 0;
    for (int i = 0; i < candidate.size(); ++i) {
        diff |= (candidate.at(i).unicode() ^ storedHash.at(i).unicode());
    }
    return diff == 0;
}
