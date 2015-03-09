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


using namespace concurrency;
using namespace Microsoft::WRL;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

/*
** The maximum pathname length supported by this VFS.
*/
#define MAXPATHNAME 512

/*
** When using this VFS, the sqlite3_file* handles that SQLite uses are
** actually pointers to instances of type WinRTFile.
*/
typedef struct
{
	sqlite3_file base;              /* Base class. Must be first. */
	IRandomAccessStream^ stream;
} WinRTFile;

// These are functions that implement the VFS "interface"
int WinRTOpen(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *pFile, int flags, int *pOutFlags);
int WinRTDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync);
int WinRTAccess(sqlite3_vfs *pVfs, const char *zPath, int flags, int *pResOut);
int WinRTFullPathname(sqlite3_vfs *pVfs, const char *zPath, int nPathOut, char *zPathOut);
void *WinRTDlOpen(sqlite3_vfs *pVfs, const char *zPath);
void WinRTDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg);
void(*WinRTDlSym(sqlite3_vfs *pVfs, void *pH, const char *z))(void);
void WinRTDlClose(sqlite3_vfs *pVfs, void *pHandle);
int WinRTRandomness(sqlite3_vfs *pVfs, int nByte, char *zByte);
int WinRTSleep(sqlite3_vfs *pVfs, int nMicro);
int WinRTCurrentTime(sqlite3_vfs *pVfs, double *pTime);

// These are the required sqlite3_io_methods to implement the sqlite3_file "interface"
int WinRTClose(sqlite3_file *pFile);
int WinRTRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite_int64 iOfst);
int WinRTWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite_int64 iOfst);
int WinRTTruncate(sqlite3_file *pFile, sqlite_int64 size);
int WinRTSync(sqlite3_file *pFile, int flags);
int WinRTFileSize(sqlite3_file *pFile, sqlite_int64 *pSize);
int WinRTLock(sqlite3_file *pFile, int eLock);
int WinRTUnlock(sqlite3_file *pFile, int eLock);
int WinRTCheckReservedLock(sqlite3_file *pFile, int *pResOut);
int WinRTFileControl(sqlite3_file *pFile, int op, void *pArg);
int WinRTSectorSize(sqlite3_file *pFile);
int WinRTDeviceCharacteristics(sqlite3_file *pFile);

// Helper functions
task<void> complete_after(unsigned int timeout);
int WinRTFlush(WinRTFile *p);
StorageFile^ GetStorageFileFromPath(const char* zPath);


