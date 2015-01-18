# SQliteRT
*		SQLiteRT - A Windows Store runtime extension for SQLite
*		for storing databases anywhere the app is allowed to access.
*
*		© Copyright 2014-2015 Peter Moore (peter@mooreusa.net). All rights reserved.
*
*		Licensed under LGPL v3 (https://www.gnu.org/licenses/lgpl.html)
*
*		How to use:
*   Add a reference to SQLiteRT to your Windows Store app project. 
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
