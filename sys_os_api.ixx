#include <Windows.h>
#include <comdef.h>

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "core_types.h"

export module sys_os_api;

//////////////////////////////////////
// MACROS
//////////////////////////////////////
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STR STRINGIZE(__LINE__)

#define RUNTIME_ERR_LINE_FILE_STR ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__


#define HR_CHECK( func )												\
do{																		\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR"\nERR: ";	\
	HRESULT hr = func;													\
	if( !SUCCEEDED( hr ) ){                                             \
        _com_error err = { hr };                                        \
		char dbgStr[1024] = {};											\
		strcat_s( dbgStr, sizeof( dbgStr ), DEV_ERR_STR );				\
		strcat_s( dbgStr, sizeof( dbgStr ), err.ErrorMessage() );	    \
		constexpr UINT32 behaviour = MB_OK | MB_ICONERROR | MB_APPLMODAL; \
        MessageBox( 0, dbgStr, 0, behaviour );                          \
		abort();														\
	}																	\
}while( 0 )	



// FILE API ------------------------------------
export{
	struct file_handle;

	file_handle SysGetFileHandle( const char* fileName, bool readOnly );
	void SysCloseFileHandle( file_handle h );
	u64 SysGetFileSize( file_handle h );
	void SysReadFile( file_handle h, void* buffer, u64 numBytesToRead );
	void SysWriteToFile( file_handle h, const u8* data, u64 sizeInBytes );
}

// TODO: might not want to crash when file can't be written/read
// TODO: multithreading
// TODO: async
// TODO: streaming
// TODO: better file api ?
struct file_handle
{
	HANDLE f;

	inline file_handle( HANDLE h ) : f{ h } {}
	inline operator HANDLE() { return f; }
};

inline file_handle SysGetFileHandle( const char* fileName, bool readOnly )
{
	DWORD accessMode;
	DWORD shareMode;
	DWORD creationDisp;
	DWORD flagsAndAttrs;
	if( readOnly )
	{
		accessMode = GENERIC_READ;
		shareMode = 0;
		creationDisp = OPEN_EXISTING;
		flagsAndAttrs = FILE_ATTRIBUTE_READONLY;
	}
	else
	{
		accessMode = GENERIC_WRITE;
		shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
		creationDisp = OPEN_ALWAYS;
		flagsAndAttrs = FILE_ATTRIBUTE_NORMAL;
	}

	HANDLE h = CreateFile( fileName, accessMode, shareMode, 0, creationDisp, flagsAndAttrs, 0 );
	assert( h != INVALID_HANDLE_VALUE );

	return h;
}
inline void SysCloseFileHandle( file_handle h )
{
	CloseHandle( h );
}
inline u64 SysGetFileSize( file_handle h )
{
	LARGE_INTEGER fileSize = {};
	HR_CHECK( HRESULT_FROM_WIN32( GetFileSizeEx( h, &fileSize ) ) );

	return fileSize.QuadPart;
}
inline void SysReadFile( file_handle h, void* buffer, u64 numBytesToRead )
{
	OVERLAPPED asyncIo = {};
	HR_CHECK( HRESULT_FROM_WIN32( !ReadFileEx( h, buffer, numBytesToRead, &asyncIo, 0 ) ) );
}
inline void SysWriteToFile( file_handle h, const u8* data, u64 sizeInBytes )
{
	OVERLAPPED asyncIo = {};
	HR_CHECK( HRESULT_FROM_WIN32( !WriteFileEx( h, data, sizeInBytes, &asyncIo, 0 ) ) );
}
