/*
*		SQLiteWinRTExtensions - A Windows Store runtime extension for SQLite
*		for storing databases anywhere the app is allowed to access.
*
*		© Copyright 2014-2015 Peter Moore (peter@mooreusa.net). All rights reserved.
*
*		Licensed under LGPL v3 (https://www.gnu.org/licenses/lgpl.html)
*
*		How to use:
*		Call SQLiteWinRTExtensions::WinRTVFS::Initialize(bool) before using any SQLite API
*		functions. Use the string "WinRTVFS" when opening databases using sqlite_open_v2
*		if WinRTVFS is not selected as the default VFS on Initialize().
*
*		SQLiteRT uses a custom VFS that opens files using StorageFile and StorageFolder,
*		and accesses them with IRandomAccessStream. This avoids the need for files to be
*		located in the ApplicationData::LocalFolder folder.
*
*		A full file path should still be given to the SQLite open functions, however. In
*		addition, your app must have access to the folder the SQLite database file is
*		located in. This access can be obtained with app manifest declarations, or
*		folder pickers.
*
*		There is much room for improvement, but this does provide the core functionality
*		necessary to use SQLite with databases located in places other than the application
*		folder.
*/

#include "pch.h"

#include <iostream>
#include <string>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <Shcore.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <ppltasks.h> 
#include <collection.h>
#include <Windows.h>
#include <robuffer.h>
#include "pch.h"

#include <ppltasks.h>
#include <agents.h>
#include <iostream>

#include <Objidl.h>
#include "WinRTVFS.h"





/*
** Open a file handle.
*/
int WinRTOpen(
	sqlite3_vfs *pVfs,              /* VFS */
	const char *zName,              /* File to open, or 0 for a temp file */
	sqlite3_file *pFile,            /* Pointer to WinRTFile struct to populate */
	int flags,                      /* Input SQLITE_OPEN_XXX flags */
	int *pOutFlags                  /* Output SQLITE_OPEN_XXX flags (or NULL) */
	)
{
	WinRTFile *p = (WinRTFile*)pFile; /* Populate this structure */

	p->base.pMethods = new sqlite3_io_methods
	{
		1,                            /* iVersion */
		WinRTClose,                    /* xClose */
		WinRTRead,                     /* xRead */
		WinRTWrite,                    /* xWrite */
		WinRTTruncate,                 /* xTruncate */
		WinRTSync,                     /* xSync */
		WinRTFileSize,                 /* xFileSize */
		WinRTLock,                     /* xLock */
		WinRTUnlock,                   /* xUnlock */
		WinRTCheckReservedLock,        /* xCheckReservedLock */
		WinRTFileControl,              /* xFileControl */
		WinRTSectorSize,               /* xSectorSize */
		WinRTDeviceCharacteristics     /* xDeviceCharacteristics */
	};

	if (zName == 0)
		return SQLITE_IOERR;

	IRandomAccessStream^ stream = nullptr;
	try
	{
		StorageFile^ file = ::GetStorageFileFromPath(zName);
		if (file == nullptr) return SQLITE_IOERR_ACCESS;

		stream = create_task(
			file->OpenAsync(
			flags & SQLITE_OPEN_READONLY ? FileAccessMode::Read : FileAccessMode::ReadWrite
			)).get();
	}
	catch (AccessDeniedException^ ex)
	{
		return SQLITE_IOERR_ACCESS;
	}

	if (pOutFlags)
		*pOutFlags = flags;

	p->stream = stream;
	return SQLITE_OK;
}

/*
** Delete the file identified by argument zPath. If the dirSync parameter
** is non-zero, then ensure the file-system modification to delete the
** file has been synced to disk before returning.
*/
int WinRTDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync)
{
	try
	{
		StorageFile^ file = ::GetStorageFileFromPath(zPath);
		auto deleteFileTask = create_task(
			file->DeleteAsync()
			);
		//if (dirSync)	
		deleteFileTask.wait();	// always wait regardless of dirSync (2015-03-16)
		return SQLITE_OK;
	}
	catch (AccessDeniedException^ ex)
	{
		return SQLITE_IOERR_ACCESS;
	}
}

/*
** Query the file-system to see if the named file exists, is readable or
** is both readable and writable. Currently a no-op (always says yes); caller is responsible
** for making sure the file is available.
*/
int WinRTAccess(
	sqlite3_vfs *pVfs,
	const char *zPath,
	int flags,
	int *pResOut
	)
{
	*pResOut = 0;
	return SQLITE_OK;
}

/*
** Argument zPath points to a null-terminated string containing a file path.
** If zPath is an absolute path, then it is copied as is into the output
** buffer. Otherwise, if it is a relative path, then the equivalent full
** path is written to the output buffer.
**
** For WinRT, all paths need to be absolute; but this would be where we might
** parse URIs if we really wanted to.
*/
int WinRTFullPathname(
	sqlite3_vfs *pVfs,              /* VFS */
	const char *zPath,              /* Input path (possibly a relative path) */
	int nPathOut,                   /* Size of output buffer in bytes */
	char *zPathOut                  /* Pointer to output buffer */
	)
{
	::memcpy(zPathOut, zPath, strlen(zPath) + 1);
	return SQLITE_OK;
}

/*
** The following four VFS methods:
**
**   xDlOpen
**   xDlError
**   xDlSym
**   xDlClose
**
** are supposed to implement the functionality needed by SQLite to load
** extensions compiled as shared objects. This simple VFS does not support
** this functionality, so the following functions are no-ops.
*/
void *WinRTDlOpen(sqlite3_vfs *pVfs, const char *zPath)
{
	return 0;
}

void WinRTDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg)
{
	// no op
}

void(*WinRTDlSym(sqlite3_vfs *pVfs, void *pH, const char *z))(void)
{
	return 0;
}

void WinRTDlClose(sqlite3_vfs *pVfs, void *pHandle)
{
	return;
}

/*
** Parameter zByte points to a buffer nByte bytes in size. Populate this
** buffer with pseudo-random data.
**
**	Not currently implemented.
*/
int WinRTRandomness(sqlite3_vfs *pVfs, int nByte, char *zByte)
{
	return SQLITE_OK;
}

/*
** Sleep for at least nMicro microseconds. Return the (approximate) number
** of microseconds slept for.
*/
int WinRTSleep(sqlite3_vfs *pVfs, int nMicro)
{
	::complete_after(nMicro / 1000).wait();
	return nMicro;
}




/*
** Close a file.
*/
int WinRTClose(sqlite3_file *pFile)
{
	WinRTFile *p = (WinRTFile*)pFile;
	int result = WinRTFlush(p);
	if (result != SQLITE_OK)
		return result;
	delete p->stream;
	delete p->base.pMethods;
	p->stream = nullptr;
	p->base.pMethods = nullptr;
	return SQLITE_OK;
}


/*
** Set *pTime to the current UTC time expressed as a Julian day. Return
** SQLITE_OK if successful, or an error code otherwise.
**
**   http://en.wikipedia.org/wiki/Julian_day
**
** This implementation is not very good. The current time is rounded to
** an integer number of seconds. Also, assuming time_t is a signed 32-bit
** value, it will stop working some time in the year 2038 AD (the so-called
** "year 2038" problem that afflicts systems that store time this way).
*/
int WinRTCurrentTime(sqlite3_vfs *pVfs, double *pTime)
{
	time_t t = time(0);
	*pTime = t / 86400.0 + 2440587.5;
	return SQLITE_OK;
}


/*
** Read data from a file.
*/
int WinRTRead(
	sqlite3_file *pFile,
	void *zBuf,
	int iAmt,
	sqlite_int64 iOfst
	)
{
	WinRTFile *p = (WinRTFile*)pFile;

	if (p->stream == nullptr)
		throw ref new Exception(
		E_HANDLE,
		"WinRTVFS Exception: SQLite database file already closed"
		);

	IInputStream^ inputStream = p->stream->GetInputStreamAt(
		iOfst
		);
	Buffer^ readBuffer = ref new Buffer(iAmt);
	IBuffer^ finalBuffer = nullptr;

	try
	{
		auto readTask = create_task(
			inputStream->ReadAsync(
			readBuffer,
			iAmt,
			InputStreamOptions::ReadAhead)
			);
		// always use the returned buffer, not the original buffer!
		finalBuffer = readTask.get();
	}
	catch (AccessDeniedException^ ex)
	{
		delete readBuffer;
		return SQLITE_IOERR_ACCESS;
	}

	ComPtr<IBufferByteAccess> bufferByteAccess;
	reinterpret_cast<IInspectable*>(finalBuffer)->QueryInterface(
		IID_PPV_ARGS(&bufferByteAccess)
		);
	BYTE* pData = nullptr;
	if (FAILED(
		bufferByteAccess->Buffer(&pData)
		))
		return SQLITE_IOERR;

	::memcpy(zBuf, pData, finalBuffer->Length);

	delete readBuffer;

	if (finalBuffer->Length < (DWORD)iAmt)
	{
		// must zero out remainder of return buffer if short read
		::memset(
			(BYTE*)zBuf + finalBuffer->Length,
			0,
			iAmt - finalBuffer->Length
			);
		return SQLITE_IOERR_SHORT_READ;
	}
	else
	{
		return SQLITE_OK;
	}
}

/*
** Write data to the file
*/
int WinRTWrite(
	sqlite3_file *pFile,
	const void *zBuf,
	int iAmt,
	sqlite_int64 iOfst
	)
{
	WinRTFile *p = (WinRTFile*)pFile;

	if (p->stream == nullptr)
		throw ref new Exception(
		E_HANDLE,
		"WinRTVFS Exception: SQLite database file already closed"
		);

	IOutputStream^ outputStream = p->stream->GetOutputStreamAt(
		iOfst
		);
	Buffer^ writeBuffer = ref new Buffer(iAmt);
	ComPtr<IBufferByteAccess> bufferByteAccess;
	reinterpret_cast<IInspectable*>(writeBuffer)->QueryInterface(
		IID_PPV_ARGS(&bufferByteAccess)
		);
	BYTE* pData = nullptr;
	if (FAILED(
		bufferByteAccess->Buffer(&pData)
		))
		return SQLITE_IOERR;

	::memcpy(pData, zBuf, iAmt);
	writeBuffer->Length = iAmt;

	int result = SQLITE_OK;
	try
	{
		auto writeTask = create_task(
			outputStream->WriteAsync(writeBuffer)
			);
		writeTask.wait();
	}
	catch (AccessDeniedException^ ex)
	{
		result = SQLITE_IOERR_ACCESS;
	}

	delete outputStream;
	delete writeBuffer;

	return result;
}

/*
** Truncate a file.
*/
int WinRTTruncate(sqlite3_file *pFile, sqlite_int64 size)
{
	WinRTFile *p = (WinRTFile*)pFile;
	if (p->stream == nullptr)
		throw ref new Exception(
		E_HANDLE,
		"WinRTVFS Exception: SQLite database file already closed"
		);
	p->stream->Size = size;
	return SQLITE_OK;
}

/*
** Sync the contents of the file to the persistent media.
*/
int WinRTSync(sqlite3_file *pFile, int flags)
{
	WinRTFile *p = (WinRTFile*)pFile;
	return WinRTFlush(p);
}

/*
** Write the size of the file in bytes to *pSize.
*/
int WinRTFileSize(sqlite3_file *pFile, sqlite_int64 *pSize)
{
	WinRTFile *p = (WinRTFile*)pFile;
	if (p->stream == nullptr)
		throw ref new Exception(
		E_HANDLE,
		"WinRTVFS Exception: SQLite database file already closed"
		);
	*pSize = p->stream->Size;
	return SQLITE_OK;
}

/*
** Locking functions. These are no-ops for WinRT
*/
int WinRTLock(sqlite3_file *pFile, int eLock)
{
	return SQLITE_OK;
}

int WinRTUnlock(sqlite3_file *pFile, int eLock)
{
	return SQLITE_OK;
}

int WinRTCheckReservedLock(sqlite3_file *pFile, int *pResOut)
{
	return SQLITE_OK;
}

/*
** No file control for this VFS.
*/
int WinRTFileControl(sqlite3_file *pFile, int op, void *pArg)
{
	return SQLITE_NOTFOUND;
}

/*
*  No-ops for WinRT
*/
int WinRTSectorSize(sqlite3_file *pFile)
{
	return 0;
}

int WinRTDeviceCharacteristics(sqlite3_file *pFile)
{
	return 0;
}


// Creates a task that completes after the specified delay.
task<void> complete_after(unsigned int timeout)
{
	// A task completion event that is set when a timer fires.
	task_completion_event<void> tce;

	// Create a non-repeating timer.
	auto fire_once = new timer<int>(timeout, 0, nullptr, false);
	// Create a call object that sets the completion event after the timer fires.
	auto callback = new call<int>([tce](int)
	{
		tce.set();
	});

	// Connect the timer to the callback and start the timer.
	fire_once->link_target(callback);
	fire_once->start();

	// Create a task that completes after the completion event is set.
	task<void> event_set(tce);

	// Create a continuation task that cleans up resources and 
	// and return that continuation task. 
	return event_set.then([callback, fire_once]()
	{
		delete callback;
		delete fire_once;
	});
}


int WinRTFlush(WinRTFile *p)
{
	int retries = 0;
	bool success = false;
	if (p->stream == nullptr)
		throw ref new Exception(E_HANDLE, "WinRTVFS Exception : SQLite database file already closed");
	while (!success && retries++ < 10)
	{
		try
		{
			create_task(
				p->stream->FlushAsync()
				).wait();
			success = true;
		}
		catch (Exception^ ex)
		{
			::WinRTSleep(nullptr, 1000000);
		}
	}
	if (!success)
		return SQLITE_IOERR_ACCESS;

	return SQLITE_OK;
}

StorageFile^ GetStorageFileFromPath(const char* zPath)
{
	int pathLength = ::strlen(zPath);
	int i;

	for (i = pathLength; i >= 0; i--)
	{
		if (zPath[i - 1] == '\\')
			break;
	}

	wchar_t* lpwstrPath = new wchar_t[i];
	int folderPathCount = ::MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, zPath, i, lpwstrPath, i);
	String^ strFolderPath = ref new String(lpwstrPath, folderPathCount);
	delete lpwstrPath;

	wchar_t* lpwstrFilename = new wchar_t[pathLength - i];
	int fileNameCount = ::MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, zPath + i, pathLength - i,

		lpwstrFilename, pathLength - i);
	String^ strFilePath = ref new String(lpwstrFilename, fileNameCount);
	delete lpwstrFilename;

	try
	{
		StorageFolder^ folder =
			create_task(
			StorageFolder::GetFolderFromPathAsync(strFolderPath)
			).get();
		if (folder == nullptr)
			return nullptr;

		StorageFile^ file =
			create_task(
			folder->CreateFileAsync(
			strFilePath,
			CreationCollisionOption::OpenIfExists
			)).get();
		if (file == nullptr)
			return nullptr;

		return file;
	}
	catch (Platform::AccessDeniedException^)
	{
		return nullptr;
	}
}