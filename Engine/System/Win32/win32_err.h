#ifndef __WIN32_ERR_H__
#define __WIN32_ERR_H__

#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "ht_error.h"

inline bool Win32IsHandleValid( HANDLE h )
{
	return ( INVALID_HANDLE_VALUE == h ) || ( 0 == h );
}

inline void Win32WriteLastErr( LPTSTR lpsLineFile )
{
	char msg[ 2048 ] = {};
	DWORD dwErr = GetLastError();

	constexpr DWORD FORMAT_MSG_FLAGS = 
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK;
	DWORD bytesFormatted = FormatMessageA( FORMAT_MSG_FLAGS, nullptr, dwErr, 0, msg, 
		( DWORD ) std::size( msg ), nullptr );

	if( 0 == bytesFormatted )
	{
		std::memset( msg, 0, std::size( msg ) );
		std::format_to_n( msg, std::size( msg ) - 1, "{} formated 0 bytes, exited with: {}", __func__, GetLastError() );
	}

	MessageBoxA( nullptr, ( LPCTSTR ) msg, TEXT( "Error" ), MB_OK | MB_ICONERROR | MB_APPLMODAL );
}

#define WIN_CHECK( winExpr )												\
do{																			\
	constexpr char WIN_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR"\nERR: ";		\
	if( !bool( winExpr ) )													\
	{																		\
		Win32WriteLastErr( ( LPSTR ) WIN_ERR_STR );							\
		abort();															\
	}																		\
}while( 0 )	

#endif // !__WIN32_ERR_H__

