#pragma

#ifndef _WIN32_ERR_
#define _WIN32_ERR_

#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#include <strsafe.h>

#include "sys_os_api.h"
#include "core_types.h"
#include "core_lib_api.h"

inline void Win32WriteLastErr( LPTSTR lpsLineFile )
{
	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				   0, dw,
				   MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
				   ( LPTSTR ) &lpMsgBuf, 0, 0 );

	lpDisplayBuf =
		( LPVOID ) LocalAlloc( LMEM_ZEROINIT, ( lstrlen( ( LPCTSTR ) lpMsgBuf ) + lstrlen( ( LPCTSTR ) lpsLineFile ) + 40 ) );
	StringCchPrintf( ( LPTSTR ) lpDisplayBuf, LocalSize( lpDisplayBuf ), TEXT( "%s code %d: %s" ), lpsLineFile, dw, lpMsgBuf );
	MessageBox( 0, ( LPCTSTR ) lpDisplayBuf, TEXT( "Error" ), MB_OK | MB_ICONERROR | MB_APPLMODAL );

	LocalFree( lpMsgBuf );
	LocalFree( lpDisplayBuf );
}

#define WIN_CHECK( win )													\
do{																			\
	constexpr char WIN_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR"\nERR: ";		\
	if( win )																\
	{																		\
		Win32WriteLastErr( ( LPSTR ) WIN_ERR_STR );							\
		abort();															\
	}																		\
}while( 0 )	

#endif // !_WIN32_ERR_
