/*
*		SQLiteRT - A Windows Store runtime extension for SQLite
*		for storing databases anywhere the app is allowed to access.
*
*		© Copyright 2014-2015 Peter Moore (peter@mooreusa.net). All rights reserved.
*
*		Licensed under LGPL v3 (https://www.gnu.org/licenses/lgpl.html)
*/

#pragma once

#include "sqlite3.h"

sqlite3_vfs* CreateWinRTVFS (void);
int ShutdownWinRTVFS (void);

