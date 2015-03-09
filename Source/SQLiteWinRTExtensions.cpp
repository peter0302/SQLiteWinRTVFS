#include "pch.h"
#include "WinRTVFS.h"

namespace SQLiteWinRTExtensions
{
	public ref class WinRTVFS sealed
	{
	public:
		static bool Initialize(bool makeDefaultVFS)
		{
			sqlite3_vfs* pVFS = new sqlite3_vfs
			{
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
				WinRTCurrentTime              /* xCurrentTime */
			};
			return (::sqlite3_vfs_register(pVFS, makeDefaultVFS) == SQLITE_OK);
		}
	};
}