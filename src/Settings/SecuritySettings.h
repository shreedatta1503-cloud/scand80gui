/****************************************************************************
 *
 * (c) 2024 QGroundControl Development Team. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtQmlIntegration/QtQmlIntegration>

#include "SettingsGroup.h"

/// \brief Persistent storage for the application-lock credential.
///
/// Only the SALTED HASH and the per-install SALT are stored — never the plain-text password.
/// Both are persisted through QGC's standard QSettings-backed SettingsFact mechanism (group
/// "Security"), so the credential survives application restarts. The hashing / verification logic
/// lives in AppLockManager; this group is pure storage.
class SecuritySettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

public:
    SecuritySettings(QObject *parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(passwordHash)    ///< Hex-encoded salted hash of the app-lock password.
    DEFINE_SETTINGFACT(passwordSalt)    ///< Hex-encoded random salt used to derive passwordHash.
};
