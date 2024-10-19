#ifndef __WIN32_UTILS__
#define __WIN32_UTILS__

#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#include <strsafe.h>
#include <stdlib.h>

#include "hell_log.hpp"
#include "macros.hpp"
#include "core_types.h"

inline void Win32WriteLastErr( LPTSTR lpsLineFile )
{
	DWORD dw = GetLastError();

	LPVOID lpMsgBuf;
	constexpr DWORD fromatFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
	FormatMessageA( fromatFlags, 0, dw, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), ( LPTSTR ) &lpMsgBuf, 0, 0 );

	LPVOID lpDisplayBuf =
		( LPVOID ) LocalAlloc( LMEM_ZEROINIT, ( lstrlen( ( LPCTSTR ) lpMsgBuf ) + lstrlen( ( LPCTSTR ) lpsLineFile ) + 40 ) );
	StringCchPrintf( ( LPTSTR ) lpDisplayBuf, LocalSize( lpDisplayBuf ), TEXT( "%s code %d: %s" ), lpsLineFile, dw, lpMsgBuf );
	// TODO: pass parent hwnd
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

#endif // !__WIN32_UTILS__

