/*
*		SQLiteRT - A Windows Store runtime extension for SQLite
*		for storing databases anywhere the app is allowed to access.
*
*		© Copyright 2014-2015 Peter Moore (peter@mooreusa.net). All rights reserved.
*
*		Licensed under LGPL v3 (https://www.gnu.org/licenses/lgpl.html)
*
*		How to use:
*		Call InitializeVFS() before using any SQLite API functions, and call ShutdownVFS()
*		on program exit to ensure all files are flushed and closed.
*
*		SQLiteRT uses a custom VFS that opens files using StorageFile and StorageFolder,
*		and accesses them with IRandomAccessStream. This avoids the need for files to be
*		located in the application folder.
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

#include <ppltasks.h>
#include <agents.h>
#include <iostream>

#include <Objidl.h>
#include "WinRTVFS.h"

using namespace concurrency;
using namespace Microsoft::WRL;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

ref class OpenFileHandle
{
public:
	property IRandomAccessStream^ Stream;
	property int RefCount;
	property String^ Path;
} ;


CRITICAL_SECTION	_criticalSection;

Map<int, OpenFileHandle^>^	_fileMap = ref new Map <int, OpenFileHandle^>;



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
	OpenFileHandle^ pOpenFileHandle;
 } WinRTFile;

static int WinRTSleep(sqlite3_vfs *pVfs, int nMicro);

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

/*
** Write directly to the file passed as the first argument. Even if the
** file has a write-buffer (WinRTFile.aBuffer), ignore it.
*/
static int WinRTDirectWrite(
	WinRTFile *p,                    /* File handle */
	const void *zBuf,               /* Buffer containing data to write */
	int iAmt,                       /* Size of data to write in bytes */
	sqlite_int64 iOfst              /* File offset to write to */
	)
{	
	int maxRetries = 10;
	BOOL access = false;
	int retries = 0;
	
	while (!(access = TryEnterCriticalSection(&_criticalSection)) && retries++ < maxRetries)
	{
		::complete_after(100).wait();
	}
	if (!access)
		return SQLITE_IOERR_ACCESS;

	

	ComPtr<IStream> pCOMStream;
	HRESULT hr = ::CreateStreamOverRandomAccessStream(
		p->pOpenFileHandle->Stream,
		IID_IStream,
		(void**)pCOMStream.GetAddressOf()
		);
	if (FAILED(hr))
	{
		throw ref new Exception(E_ABORT, "Failed to get IStream for file");
	}

	ULARGE_INTEGER newPosition;
	hr = pCOMStream->Seek(
		(LARGE_INTEGER)(*((LARGE_INTEGER*)(&iOfst))),
		STREAM_SEEK_SET,
		&newPosition);
	if (!SUCCEEDED(hr))
	{
		::LeaveCriticalSection(&_criticalSection);
		return SQLITE_FAIL;
	}

	ULONG ulWritten;
	hr = pCOMStream->Write(zBuf, iAmt, &ulWritten);
	if (!SUCCEEDED(hr))
	{
		::LeaveCriticalSection(&_criticalSection);
		return SQLITE_FAIL;
	}
	if (ulWritten != (ULONG)iAmt )
	{
		::LeaveCriticalSection(&_criticalSection);
		return SQLITE_IOERR_LOCK;
	}

	hr = pCOMStream->Commit(STGC_DEFAULT);

	LeaveCriticalSection(&_criticalSection);

	if (!SUCCEEDED(hr))
		return SQLITE_FAIL;

	return SQLITE_OK;
}

/*
* Currently a no-op because the buffers will be flushed when the IRandomAccessStream is finally closed.
*/
static int WinRTFlushBuffer(WinRTFile *p)
{
	return SQLITE_OK;	
}



/*
** Close a file.
*/
static int WinRTClose(sqlite3_file *pFile)
{
	WinRTFile *p = (WinRTFile*)pFile;
		
	::EnterCriticalSection(&_criticalSection);

	OpenFileHandle^ OpenFileHandle = p->pOpenFileHandle;
	if (--OpenFileHandle->RefCount == 0)
	{
		IAsyncOperation<bool>^ op = p->pOpenFileHandle->Stream->FlushAsync();
		while (op->Status == AsyncStatus::Started);

		delete OpenFileHandle->Stream;
		_fileMap->Remove(OpenFileHandle->Path->GetHashCode());
		delete OpenFileHandle;
	}

	::LeaveCriticalSection(&_criticalSection);
	return SQLITE_OK;
}

/*
** Read data from a file.
*/
static int WinRTRead(
	sqlite3_file *pFile,
	void *zBuf,
	int iAmt,
	sqlite_int64 iOfst
	)
{
	WinRTFile *p = (WinRTFile*)pFile;
	int maxRetries = 10;

	LARGE_INTEGER seek;
	seek.QuadPart = (LONGLONG)iOfst;
	
	BOOL access = false;
	int retries = 0;
	while (!(access = TryEnterCriticalSection(&_criticalSection)) && retries++ < maxRetries)
	{
		::complete_after(100).wait();
	}
	if (!access)
		return SQLITE_IOERR_ACCESS;
	
	Microsoft::WRL::ComPtr<IStream> pCOMStream;
	HRESULT hr = ::CreateStreamOverRandomAccessStream (
		p->pOpenFileHandle->Stream, 
		IID_IStream, 
		(void**)pCOMStream.GetAddressOf() 
		);
	if (FAILED(hr))
	{
		throw ref new Exception(E_ABORT, "Failed to get IStream for file");
	}

	ULARGE_INTEGER newPosition;
	hr = pCOMStream->Seek(
		seek,
		STREAM_SEEK_SET,
		&newPosition);
	if (!SUCCEEDED(hr))
	{
		::LeaveCriticalSection(&_criticalSection);
		return SQLITE_IOERR_READ;
	}

	int retryCount = 0;

retry:

	ULONG pchRead;
	hr = pCOMStream->Read(zBuf, iAmt, &pchRead);
	if (!SUCCEEDED(hr))
	{
		if (hr != E_ACCESSDENIED)
		{
			::LeaveCriticalSection(&_criticalSection);
			return SQLITE_IOERR_READ;
		}
		if (retryCount++ < maxRetries)
		{
			WinRTSleep(nullptr, 100000);
			goto retry;
		}
		::LeaveCriticalSection(&_criticalSection);
		return SQLITE_IOERR_READ;
	}

	::LeaveCriticalSection(&_criticalSection);

	if (pchRead < (UINT32)iAmt)
	{
		/* Unread parts of the buffer must be zero-filled */
		::memset(&((char*)zBuf)[pchRead], 0, iAmt - pchRead);
		return SQLITE_IOERR_SHORT_READ;
	}

	return SQLITE_OK;
}

/*
** Write data to the file
*/
static int WinRTWrite(
	sqlite3_file *pFile,
	const void *zBuf,
	int iAmt,
	sqlite_int64 iOfst
	)
{
	WinRTFile *p = (WinRTFile*)pFile;	
	return WinRTDirectWrite(p, zBuf, iAmt, iOfst);		
}

/*
** Truncate a file. This is a no-op for this VFS.
*/
static int WinRTTruncate(sqlite3_file *pFile, sqlite_int64 size)
{
	return SQLITE_OK;
}

/*
** Sync the contents of the file to the persistent media.
*/
static int WinRTSync(sqlite3_file *pFile, int flags)
{
	WinRTFile *p = (WinRTFile*)pFile;

	Microsoft::WRL::ComPtr<IStream> pCOMStream;
	HRESULT hr = ::CreateStreamOverRandomAccessStream(
		p->pOpenFileHandle->Stream,
		IID_IStream,
		(void**)pCOMStream.GetAddressOf()
		);
	if (FAILED(hr))
	{
		throw ref new Exception(E_ABORT, "Failed to get IStream for file");
	}

	hr = pCOMStream->Commit(STGC_DEFAULT);
	if (!SUCCEEDED(hr))
		return SQLITE_IOERR_FSYNC;

	return SQLITE_OK;
}

/*
** Write the size of the file in bytes to *pSize.
*/
static int WinRTFileSize(sqlite3_file *pFile, sqlite_int64 *pSize)
{
	WinRTFile *p = (WinRTFile*)pFile;
	*pSize = p->pOpenFileHandle->Stream->Size;
	return SQLITE_OK;
}

/*
** Locking functions. These are no-ops for WinRT
*/
static int WinRTLock(sqlite3_file *pFile, int eLock)
{
	return SQLITE_OK;
}

static int WinRTUnlock(sqlite3_file *pFile, int eLock)
{
	return SQLITE_OK;
}

static int WinRTCheckReservedLock(sqlite3_file *pFile, int *pResOut)
{
	return SQLITE_OK;
}

/*
** No file control for this VFS.
*/
static int WinRTFileControl(sqlite3_file *pFile, int op, void *pArg)
{
	return SQLITE_NOTFOUND;
}

/*
*  No-ops for WinRT
*/
static int WinRTSectorSize(sqlite3_file *pFile)
{
	return 0;
}

static int WinRTDeviceCharacteristics(sqlite3_file *pFile)
{
	return 0;
}



/*
*  StorageFolder::CreateFileAsync needs this to create a file in a particular folder
*/
String^ GetFileNameFromFullPath(String^ path)
{
	const wchar_t* data = path->Data();
	int i = path->Length();
	for (; i >= 0; i--)
	{
		if (data[i] == '\\')
			break;			
	}
	
	int newStrLen = path->Length() - i;
	wchar_t* newString = new wchar_t[newStrLen];
	::memcpy(newString, data + i + 1, newStrLen * sizeof(wchar_t));

	String^ strNewString = ref new String(newString);
	delete newString;

	return strNewString;
}


/*
*  Need this to eventually get a StorageFolder from a full path
*/
String^ GetFolderPathFromFullPath(String^ path)
{
	const wchar_t* data = path->Data();
	int i = path->Length();
	for (; i >= 0; i--)
	{
		if (data[i] == '\\')
			break;
	}

	int newStrLen = i;
	wchar_t* newString = new wchar_t[newStrLen + 1];
	::memcpy(newString, data, newStrLen * sizeof(wchar_t));
	newString[newStrLen] = 0x00;

	String^ strNewString = ref new String(newString);
	delete newString;

	return strNewString;
}


/*
** Open a file handle.
*/
static int WinRTOpen(
	sqlite3_vfs *pVfs,              /* VFS */
	const char *zName,              /* File to open, or 0 for a temp file */
	sqlite3_file *pFile,            /* Pointer to WinRTFile struct to populate */
	int flags,                      /* Input SQLITE_OPEN_XXX flags */
	int *pOutFlags                  /* Output SQLITE_OPEN_XXX flags (or NULL) */
	)
{
	static const sqlite3_io_methods WinRTio = {
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

	WinRTFile *p = (WinRTFile*)pFile; /* Populate this structure */
	int oflags = 0;                 /* flags to pass to open() call */
	char *aBuf = 0;

	if (zName == 0)
	{
		return SQLITE_IOERR;
	}

	IRandomAccessStream^ operativeStream = nullptr;

	wchar_t lpwstrPath[256];
	::MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, zName, -1, lpwstrPath, 256);
	String^ strPath = ref new String(lpwstrPath);
	
	::EnterCriticalSection(&_criticalSection);

	// try to find an open file stream first
	if ( _fileMap->HasKey(strPath->GetHashCode()) )
	{		
		OpenFileHandle^ pOpenFileHandle = _fileMap->Lookup(strPath->GetHashCode());
		operativeStream = pOpenFileHandle->Stream;
		pOpenFileHandle->RefCount++;
		p->pOpenFileHandle = pOpenFileHandle;		
	}
	else
	{
		// try to open or possibly create the file
		IAsyncOperation<StorageFolder^>^ getFolderOp = StorageFolder::GetFolderFromPathAsync(::GetFolderPathFromFullPath(strPath));
		while (getFolderOp->Status == AsyncStatus::Started);
		StorageFolder^ folder = getFolderOp->GetResults();

		StorageFile^ file = nullptr;
		IAsyncOperation<StorageFile^>^ openOp = folder->CreateFileAsync(
			::GetFileNameFromFullPath(strPath),
			CreationCollisionOption::OpenIfExists
			);
		while (openOp->Status == AsyncStatus::Started);
		file = openOp->GetResults();

		IAsyncOperation<IRandomAccessStream^>^ openStreamOp;
		openStreamOp = file->OpenAsync(FileAccessMode::ReadWrite);
		while (openStreamOp->Status == AsyncStatus::Started);
		operativeStream = openStreamOp->GetResults();		

		OpenFileHandle^ pOpenFileHandle = ref new OpenFileHandle;
		pOpenFileHandle->Stream = operativeStream;
		pOpenFileHandle->RefCount++;
		pOpenFileHandle->Path = strPath;
		p->pOpenFileHandle = pOpenFileHandle;

		// Record that we have an open IRandomAccessStream for this file path
		_fileMap->Insert(strPath->GetHashCode(), pOpenFileHandle);		
	}

	::LeaveCriticalSection(&_criticalSection);	

	if (pOutFlags)
	{
		*pOutFlags = flags;
	}
	p->base.pMethods = &WinRTio;
	return SQLITE_OK;
}

/*
** Delete the file identified by argument zPath. If the dirSync parameter
** is non-zero, then ensure the file-system modification to delete the
** file has been synced to disk before returning.
*/
static int WinRTDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync)
{	
	// try to open or possibly create the file
	wchar_t lpwstrPath[256];
	::MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, zPath, -1, lpwstrPath, 256);
	String^ strPath = ref new String(lpwstrPath);

	String^ strFolderPath = ::GetFolderPathFromFullPath(strPath);
	IAsyncOperation<StorageFolder^>^ getFolderOp = StorageFolder::GetFolderFromPathAsync(strFolderPath);
	while (getFolderOp->Status == AsyncStatus::Started);
	StorageFolder^ folder = getFolderOp->GetResults();

	IAsyncOperation<StorageFile^>^ openOp;
	openOp = folder->CreateFileAsync(::GetFileNameFromFullPath(strPath), CreationCollisionOption::OpenIfExists);
	while (openOp->Status == AsyncStatus::Started);
	StorageFile^ file = openOp->GetResults();

	IAsyncAction^ deleteAction = file->DeleteAsync();
	if (dirSync)
		while (deleteAction->Status == AsyncStatus::Started);

	return SQLITE_OK;	
}

#ifndef F_OK
# define F_OK 0
#endif
#ifndef R_OK
# define R_OK 4
#endif
#ifndef W_OK
# define W_OK 2
#endif

/*
** Query the file-system to see if the named file exists, is readable or
** is both readable and writable. Currently a no-op; caller is responsible
** for making sure the file is available.
*/
static int WinRTAccess(
	sqlite3_vfs *pVfs,
	const char *zPath,
	int flags,
	int *pResOut
	)
{
	int rc;                         /* access() return code */
	int eAccess = F_OK;             /* Second argument to access() */

	assert(
		flags == SQLITE_ACCESS_EXISTS       /* access(zPath, F_OK) */
		|| flags == SQLITE_ACCESS_READ         /* access(zPath, R_OK) */
		|| flags == SQLITE_ACCESS_READWRITE    /* access(zPath, R_OK|W_OK) */
		);

	if (flags == SQLITE_ACCESS_READWRITE) eAccess = R_OK | W_OK;
	if (flags == SQLITE_ACCESS_READ)      eAccess = R_OK;

	rc = 1;
	*pResOut = (rc == 0);
	return SQLITE_OK;
}

/*
** Argument zPath points to a null-terminated string containing a file path.
** If zPath is an absolute path, then it is copied as is into the output
** buffer. Otherwise, if it is a relative path, then the equivalent full
** path is written to the output buffer.
**
** This function assumes that paths are UNIX style. Specifically, that:
**
**   1. Path components are separated by a '/'. and
**   2. Full paths begin with a '/' character.
*/
static int WinRTFullPathname(
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
static void *WinRTDlOpen(sqlite3_vfs *pVfs, const char *zPath)
{
	return 0;
}

static void WinRTDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg)
{
	sqlite3_snprintf(nByte, zErrMsg, "Loadable extensions are not supported");
	zErrMsg[nByte - 1] = '\0';
}

static void(*WinRTDlSym(sqlite3_vfs *pVfs, void *pH, const char *z))(void)
{
	return 0;
}

static void WinRTDlClose(sqlite3_vfs *pVfs, void *pHandle){
	return;
}

/*
** Parameter zByte points to a buffer nByte bytes in size. Populate this
** buffer with pseudo-random data. Not currently implemented.
*/
static int WinRTRandomness(sqlite3_vfs *pVfs, int nByte, char *zByte)
{	
	return SQLITE_OK;
}

/*
** Sleep for at least nMicro microseconds. Return the (approximate) number
** of microseconds slept for.
*/
static int WinRTSleep(sqlite3_vfs *pVfs, int nMicro)
{
	::complete_after(nMicro / 1000).wait();
	return nMicro;
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
static int WinRTCurrentTime(sqlite3_vfs *pVfs, double *pTime)
{
	time_t t = time(0);
	*pTime = t / 86400.0 + 2440587.5;
	return SQLITE_OK;
}

/*
** This function returns a pointer to the VFS implemented in this file.
** To make the VFS available to SQLite:
**
**   sqlite3_vfs_register(sqlite3_WinRTvfs(), 0);
*/
sqlite3_vfs *CreateWinRTVFS()
{
	static sqlite3_vfs WinRTvfs = {
		1,                            /* iVersion */
		sizeof(WinRTFile),             /* szOsFile */
		MAXPATHNAME,                  /* mxPathname */
		0,                            /* pNext */
		"WinRTVFS",                   /* zName */
		(void*)0,					  /* pAppData */
		WinRTOpen,                     /* xOpen */
		WinRTDelete,                   /* xDelete */
		WinRTAccess,                   /* xAccess */
		WinRTFullPathname,             /* xFullPathname */
		WinRTDlOpen,                   /* xDlOpen */
		WinRTDlError,                  /* xDlError */
		WinRTDlSym,                    /* xDlSym */
		WinRTDlClose,                  /* xDlClose */
		WinRTRandomness,               /* xRandomness */
		WinRTSleep,                    /* xSleep */
		WinRTCurrentTime,              /* xCurrentTime */
	};

	::InitializeCriticalSectionEx(&_criticalSection, 0, 0);

	return &WinRTvfs;
}


/*
*	This must be called when the program terminates to make sure
*	any open file streams are flushed and closed.
*/
int ShutdownWinRTVFS(void)
{
	BOOL access = false;
	int retries = 0, maxRetries = 10;
	while (!(access = TryEnterCriticalSection(&_criticalSection)) && retries++ < maxRetries)
	{
		::complete_after(100).wait();
	}
	if (!access)
		return SQLITE_IOERR_ACCESS;

	for (auto pair : _fileMap)
	{
		IAsyncOperation<bool>^ op = pair->Value->Stream->FlushAsync();
		while (op->Status == AsyncStatus::Started);

		delete pair->Value->Stream;
	}

	::LeaveCriticalSection(&_criticalSection);
	return SQLITE_OK;
}