#include <System/sys_platform.hpp>

#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include <errhandlingapi.h>

#include "win32_platform.hpp"
#include "win32_utils.hpp"

inline void win32_window::SetUserData( uintptr_t pData )
{
	WIN_CHECK( !SetWindowLongPtr( hwnd, GWLP_USERDATA, static_cast<LONG_PTR>( pData ) ) );
}

inline u64	SysGetCpuFreq()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	return freq.QuadPart;
}
inline u64	SysTicks()
{
	LARGE_INTEGER tick;
	QueryPerformanceCounter( &tick );
	return  tick.QuadPart;
}

// TODO: use void ?
inline u64	SysDllLoad( const char* name )
{
	static_assert( sizeof( u64 ) == sizeof( HMODULE ) );
	return ( u64 ) LoadLibraryA( name );
}
inline void	SysDllUnload( u64 hDll )
{
	if( !hDll ) return;
	WIN_CHECK( !FreeLibrary( ( HMODULE ) hDll ) );
}
inline void* SysGetProcAddr( u64 hDll, const char* procName )
{
	static_assert( sizeof( void* ) == sizeof( FARPROC ) );
	return ( void* ) GetProcAddress( ( HMODULE ) hDll, procName );
}

static inline void SysDbgPrint( const char* str )
{
	OutputDebugString( str );
}
inline void SysErrMsgBox( const char* str )
{
	UINT behaviour = MB_OK | MB_ICONERROR | MB_APPLMODAL;
	MessageBox( 0, str, 0, behaviour );
}

static inline bool SysPumpPlatfromMessages()
{
	MSG msg;
	while( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ) )
	{
		TranslateMessage( &msg );
		DispatchMessageA( &msg );

		if( msg.message == WM_QUIT )
		{
			return false;
		}
	}

	return true;
}

