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


#include <stdlib.h>
#include "pch.h"
#include "sqlite3.h"
#include "WinRTVFS.h"

using namespace ::Platform;
using namespace Windows::Storage::Streams;

namespace SQLiteRT
{
	public ref class SQLiteDBHandle sealed
	{
	internal:
		sqlite3*	_pHandle;

	public:
		ULONGLONG	GetPointer()
		{
			return (ULONGLONG)_pHandle;
		}
	};

	public ref class SQLiteStatement sealed
	{
	internal:
		sqlite3_stmt*	_pStatement;

	public:
		ULONGLONG GetPointer()
		{
			return (ULONGLONG)_pStatement;
		}
	};


	public ref class SQLiteAPI sealed
	{
	private:
		static void RTStringToCString(String^ rtString, char** ppString)
		{
			DWORD32 length = rtString->Length();
			*ppString = new char[length + 1];
			const wchar_t* pwData = rtString->Data();
			for (DWORD32 i = 0; i < length; i++)
			{
				(*ppString)[i] = (char)pwData[i];
			}
			(*ppString)[length] = 0;
		}


	public:
		static DWORD32 InitializeVFS()
		{
			sqlite3_vfs* winRTVFS = ::CreateWinRTVFS();
			return ::sqlite3_vfs_register(winRTVFS, true);
		}

		static DWORD32 ShutdownVFS()
		{
			return ::ShutdownWinRTVFS();
		}

		static DWORD32 sqlite3_open(String^ filename, SQLiteDBHandle^ db)
		{
			char* szFilename;
			RTStringToCString(filename, &szFilename);
			DWORD32 result = ::sqlite3_open(szFilename, &db->_pHandle);
			delete szFilename;
			return result;
		}				

		static DWORD32 Open(const Array<BYTE>^ filename, SQLiteDBHandle^ db, int flags, String^ zvfs)
		{
			char* szVFS = nullptr;
			if ( zvfs != nullptr && zvfs != "" ) 
				RTStringToCString(zvfs, &szVFS);
			DWORD32 result = ::sqlite3_open_v2((char*)(filename->Data), &db->_pHandle, flags, szVFS);
			delete szVFS;
			return result;
		}

		static DWORD32 Open16(String^ filename, SQLiteDBHandle^ db)
		{
			char* szFilename;
			RTStringToCString(filename, &szFilename);
			DWORD32 result = ::sqlite3_open16(szFilename, &db->_pHandle);
			delete szFilename;
			return result;
		}

		static DWORD32 Close(SQLiteDBHandle^ db)
		{
			return ::sqlite3_close(db->_pHandle);
		}

		static UINT32 Changes(SQLiteDBHandle^ db)
		{
			return ::sqlite3_changes(db->_pHandle);						
		}

		static INT32 BindBlob(SQLiteStatement^ stmt, int index, const Array<BYTE>^ val, int n, DWORD32 free)
		{
			return ::sqlite3_bind_blob(stmt->_pStatement, index, val->Data, n, (void(*)(void*))(free));
		}

		static INT32 BindInt(SQLiteStatement^ stmt, int index, int val)
		{
			return ::sqlite3_bind_int(stmt->_pStatement, index, val);
		}

		static INT32 BindInt64(SQLiteStatement^ stmt, int index, LONGLONG val)
		{
			return ::sqlite3_bind_int64(stmt->_pStatement, index, val);
		}

		static INT32 BindDouble(SQLiteStatement^ stmt, int index, double val)
		{
			return ::sqlite3_bind_double(stmt->_pStatement, index, val);
		}

		static INT32 BindParameterIndex(SQLiteStatement^ stmt, String^ name)
		{
			char* szName;
			RTStringToCString(name, &szName);
			INT32 result = ::sqlite3_bind_parameter_index(stmt->_pStatement, szName);
			delete szName;
			return result;
		}

		static INT32 BindNull(SQLiteStatement^ stmt, INT32 index)
		{
			return ::sqlite3_bind_null(stmt->_pStatement, index);
		}

		static INT32 BindText16(SQLiteStatement^ stmt, INT32 index, String^ val, INT32 n, DWORD32 free)
		{
			INT32 result = ::sqlite3_bind_text16(stmt->_pStatement, index, val->Data(), n, (void(*)(void*))(free));
			return result;
		}

		static DWORD32 Config(INT32 option)
		{
			return ::sqlite3_config(option);
		}

		static DWORD32 BusyTimeout(SQLiteDBHandle^ db, INT32 milliseconds)
		{
			return ::sqlite3_busy_timeout(db->_pHandle, milliseconds);
		}

		static INT32 ColumnType(SQLiteStatement^ stmt, int index)
		{
			return ::sqlite3_column_type(stmt->_pStatement, index);
		}

		static void ColumnBlob(SQLiteStatement^ stmt, int index, const Array<BYTE>^ buffer)
		{
			BYTE* result = (BYTE*)::sqlite3_column_blob(stmt->_pStatement, index);
			::memcpy(buffer->Data, result, buffer->Length);
		}

		static INT32 ColumnBytes(SQLiteStatement^ stmt, int index)
		{
			return ::sqlite3_column_bytes(stmt->_pStatement, index);
		}

		static INT32 ColumnCount(SQLiteStatement^ stmt)
		{
			return ::sqlite3_column_count(stmt->_pStatement);
		}

		static String^ ColumnName16(SQLiteStatement^ stmt, int index)
		{
			const wchar_t* result = (wchar_t*)::sqlite3_column_name16(stmt->_pStatement, index);
			return ref new String(result);
		}

		static INT32 ColumnInt(SQLiteStatement^ stmt, int index)
		{
			return ::sqlite3_column_int(stmt->_pStatement, index);
		}

		static LONGLONG ColumnInt64(SQLiteStatement^ stmt, int index)
		{
			return ::sqlite3_column_int64(stmt->_pStatement, index);
		}

		static DOUBLE ColumnDouble(SQLiteStatement^ stmt, int index)
		{
			return ::sqlite3_column_double(stmt->_pStatement, index);
		}

		static String^ ColumnText16(SQLiteStatement^ stmt, int index)
		{
			const wchar_t* result = (const wchar_t*)::sqlite3_column_text16(stmt->_pStatement, index);
			return ref new String(result);
		}

		static INT32 sqlite3_extended_errcode(SQLiteDBHandle^ db)
		{
			return ::sqlite3_extended_errcode(db->_pHandle);
		}

		static INT32 sqlite3_libversion_number()
		{
			return ::sqlite3_libversion_number();
		}

		static DWORD32 Finalize(SQLiteStatement^ stmt)
		{
			return ::sqlite3_finalize(stmt->_pStatement);
		}

		static String^ GetErrmsg(SQLiteDBHandle^ db)
		{
			const wchar_t* result = (const wchar_t*)::sqlite3_errmsg16(db->_pHandle);
			return ref new String(result);
		}

		static DWORD32 Prepare2(SQLiteDBHandle^ db, String^ sql, INT32 numBytes, SQLiteStatement^ stmt)//, IntPtr pzTail)
		{
			char* szSql;
			RTStringToCString(sql, &szSql);
			DWORD32 result = ::sqlite3_prepare_v2(db->_pHandle, szSql, numBytes, &stmt->_pStatement, 0);// (const char**)pzTail.ToInt32());
			delete szSql;
			return result;
		}

		static DWORD32 Step(SQLiteStatement^ stmt)
		{
			return ::sqlite3_step(stmt->_pStatement);
		}

		static DWORD32 Reset(SQLiteStatement^ stmt)
		{
			return ::sqlite3_reset(stmt->_pStatement);
		}

		static LONGLONG LastInsertRowid(SQLiteDBHandle^ db)
		{
			return ::sqlite3_last_insert_rowid(db->_pHandle);
		}
	};			
	
}