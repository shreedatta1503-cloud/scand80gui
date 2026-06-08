/****************************************************************************
 *
 * (c) 2024 QGroundControl Development Team. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "SecuritySettings.h"

// The second macro argument is the QSettings group name used to persist these facts.
DECLARE_SETTINGGROUP(Security, "Security")
{
}

DECLARE_SETTINGSFACT(SecuritySettings, passwordHash)
DECLARE_SETTINGSFACT(SecuritySettings, passwordSalt)
