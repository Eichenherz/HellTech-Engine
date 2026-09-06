#include <System/Win32/DEFS_WIN32_NO_BS.h>
#include <Windows.h>
#pragma comment( lib, "Synchronization.lib" )
#include <windowsx.h>
#include <hidusage.h>

#include <ht_core_types.h>

#include <System/Win32/win32_err.h>
#include <System/sys_sync.h>
#include <System/sys_thread.h>
#include <System/sys_timer.h>
#include <System/sys_std_streams.h>

#include "engine_platform_api.h"
#include <ht_memory.h>
#include <ht_mem_arena.h>

#include <System/Win32/win32_kbd_scancodes.h>


//===================GLOBALS====================//
u64                         gNumCores           = 0;
job_system_ctx*             pJobSys	            = nullptr;
thread_local thread_ctx*    pThreadCtx          = nullptr;
linear_arena*               pPersistentArena    = nullptr;
//==============================================//

static u64 SysGetPhysicalCoreCount()
{
    constexpr LOGICAL_PROCESSOR_RELATIONSHIP relType = RelationProcessorCore;

    ht_mem_scope memScope = { *pPersistentArena };

    DWORD buffSzInBytes = 0;
    GetLogicalProcessorInformationEx( relType, nullptr, &buffSzInBytes );
    WIN_CHECK( ERROR_INSUFFICIENT_BUFFER == GetLastError() );

    std::span buff = ArenaNewArray<u8>( *pPersistentArena, buffSzInBytes );
    WIN_CHECK( GetLogicalProcessorInformationEx(
            relType, ( SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* ) std::data( buff ), &buffSzInBytes ) );

    u64 physicalCoreCount = 0;
    for( DWORD byteOffset = 0; byteOffset < buffSzInBytes; )
    {
        const auto& infoEx = *( SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* ) ( std::data( buff ) + byteOffset );
        byteOffset += infoEx.Size;
        physicalCoreCount++;
    }
    return physicalCoreCount;
}

static void SysOsCreateConsole()
{
	WIN_CHECK( AllocConsole() );
	// NOTE: https://alexanderhoughton.co.uk/blog/redirect-all-stdout-stderr-to-console/
	//WIN_CHECK( !AttachConsole( GetCurrentProcessId() ) );
	HANDLE hConOut = CreateFileA( "CONOUT$", GENERIC_WRITE,
		FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
		0, nullptr );
	WIN_CHECK( INVALID_HANDLE_VALUE != hConOut );
	WIN_CHECK( SetStdHandle( STD_OUTPUT_HANDLE, hConOut ) );
	WIN_CHECK( SetStdHandle( STD_ERROR_HANDLE, hConOut ) );
}

static bool SysPumpUserInput()
{
	MSG msg;
	while( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ) )
	{
		TranslateMessage( &msg );
		DispatchMessageA( &msg );
		if( WM_QUIT == msg.message ) return false;
	}

	return true;
}

static void Win32ProcessRawInput( const RAWINPUT& ri, ht_input_state& inputState )
{
	if( RIM_TYPEKEYBOARD == ri.header.dwType )
	{
		const RAWKEYBOARD& kb = ri.data.keyboard;
		if( KEYBOARD_OVERRUN_MAKE_CODE == kb.MakeCode ) return;

		bool    isE0        = kb.Flags & RI_KEY_E0;
		u16     keyIndex    = ( u16 ) ( kb.MakeCode | ( isE0 ? 0x100 : 0 ) );
		bool    isPressed   = !( kb.Flags & RI_KEY_BREAK );
		inputState.UpdateButtonState( keyIndex, isPressed );
	}
	if( RIM_TYPEMOUSE == ri.header.dwType )
	{
		if( !( ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) )
		{
			inputState.mouseDx += ri.data.mouse.lLastX;
			inputState.mouseDy += ri.data.mouse.lLastY;
		}

		USHORT usButtonFlags = ri.data.mouse.usButtonFlags;
		if( usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN )   inputState.UpdateButtonState( HT_MB_LEFT, 1 );
		if( usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP )     inputState.UpdateButtonState( HT_MB_LEFT, 0 );
		if( usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN )  inputState.UpdateButtonState( HT_MB_RIGHT, 1 );
		if( usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP )    inputState.UpdateButtonState( HT_MB_RIGHT, 0 );
		if( usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN ) inputState.UpdateButtonState( HT_MB_MIDDLE, 1 );
		if( usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP )   inputState.UpdateButtonState( HT_MB_MIDDLE, 0 );
		if( usButtonFlags & RI_MOUSE_BUTTON_4_DOWN )      inputState.UpdateButtonState( HT_MB_4, 1 );
		if( usButtonFlags & RI_MOUSE_BUTTON_4_UP )        inputState.UpdateButtonState( HT_MB_4, 0 );
		if( usButtonFlags & RI_MOUSE_BUTTON_5_DOWN )      inputState.UpdateButtonState( HT_MB_5, 1 );
		if( usButtonFlags & RI_MOUSE_BUTTON_5_UP )        inputState.UpdateButtonState( HT_MB_5, 0 );
	}
}

// TODO: interp mouse pos wrt frameTime
static ht_input_state globalHtInputState = {};

LRESULT CALLBACK MainWndProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch( uMsg )
	{
		// TODO: this will exit the Loop immediately.
		case WM_CLOSE: case WM_DESTROY:  PostQuitMessage( 0 ); break;
		case WM_MOUSEMOVE:
		{
			globalHtInputState.mousePos = {
			    ( float ) GET_X_LPARAM( lParam ),
			    ( float ) GET_Y_LPARAM( lParam )
			};
			break;
		}
		
		case WM_INPUT:
		{
			HRAWINPUT hri = ( HRAWINPUT ) lParam;

		    constexpr UINT cbHeaderSz   = sizeof( RAWINPUTHEADER );

			UINT size = 0;
			if( ( -1 == GetRawInputData( hri, RID_INPUT, nullptr, &size, cbHeaderSz ) ) || !size )
			{
				break;
			}

		    std::span scratchPad = {
			    ( u8* ) pThreadCtx->scratchArenas[ 1 ].Alloc( size, alignof( RAWINPUT ) ), size
			};

			if( -1 == GetRawInputData( hri, RID_INPUT, std::data( scratchPad ), &size, cbHeaderSz ) )
			{
				break;
			}

			const RAWINPUT& ri = *( const RAWINPUT* ) std::data( scratchPad );
			Win32ProcessRawInput( ri, globalHtInputState );

			break;
		}
	}
	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}


using sys_physical_path = fixed_string<MAX_PATH>;

static std::span<thread_ctx>    threadCtxArray  = {};
static linear_arena             persistentArena = {};

u64 HtCurrentThreadIdx()
{
    HT_ASSERT( pThreadCtx && std::size( threadCtxArray ) );
    return pThreadCtx - std::data( threadCtxArray );
}

UINT WINAPI Win32ThreadLoop( LPVOID lpParam )
{
    u64 threadIdx   = ( u64 ) lpParam;
    pThreadCtx      = &threadCtxArray[ threadIdx ];

	for( ;; )
	{
		SysSemaphoreWait( pJobSys->sema, INFINITE );

		for( job_t job = {}; pJobSys->queue.TryPop( job ); )
		{
		    ht_mem_scope jobScope = { pThreadCtx->scratchArenas[ 0 ] };
			job.PfnJob( job.payload, &pThreadCtx->scratchArenas[ 0 ] );
		}
	}

	return 0;
}

INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR, INT )
{
	SysOsCreateConsole();

	SysNameThread( ( u64 ) GetCurrentThread(), L"Main Thread" );

#ifdef  _DEBUG
	{
	    char workingDir[ MAX_PATH ] = {};
	    WIN_CHECK( 0 != GetCurrentDirectoryA( std::size( workingDir ), workingDir ) );

	    fixed_string<512> workingDirMsg = { "WorkingDir: {}\n", workingDir };
	    SysWriteToStdStream( ( const char* ) workingDirMsg, sys_stream_t::OUTPUT );
	}
#endif //_DEBUG

	WIN_CHECK( DirectX::XMVerifyCPUSupport() );

	SYSTEM_INFO sysInfo = {};
	GetSystemInfo( &sysInfo );

	HT_ASSERT( OS_COMMIT_PAGE_SIZE_IN_BYTES == sysInfo.dwPageSize );
	// NOTE: we only support level 4 paging no LA57
	HT_ASSERT( OS_USER_MAX_ADDR == ( u64 ) sysInfo.lpMaximumApplicationAddress );

	WNDCLASSEX wc = {
		.cbSize			= sizeof( WNDCLASSEX ),
		.lpfnWndProc	= MainWndProc,
		.hInstance		= hInst,
		.hCursor		= LoadCursor( NULL, IDC_ARROW ),
		.lpszClassName	= ENGINE_NAME
	};
	WIN_CHECK( RegisterClassExA( &wc ) );

	LONG left	= 140;
	LONG top	= 60;
	RECT wr		= {
		.left	= left,
		.top	= top,
		.right	= ( LONG ) SCREEN_WIDTH + left,
		.bottom = ( LONG ) SCREEN_HEIGHT + top
	};

	constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRect( &wr, windowStyle, FALSE );
	HWND hWnd = CreateWindow( wc.lpszClassName, WINDOW_TITLE, windowStyle, wr.left, wr.top,
		wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, hInst, NULL );
	WIN_CHECK( INVALID_HANDLE_VALUE != hWnd );

	ShowWindow( hWnd, SW_SHOWDEFAULT );

	// NOTE: don't use RIDEV_INPUTSINK in order to only receive when in focus
	RAWINPUTDEVICE hid[] = {
		RAWINPUTDEVICE{
			.usUsagePage	= HID_USAGE_PAGE_GENERIC,
			.usUsage		= HID_USAGE_GENERIC_MOUSE,
			.dwFlags		= 0, // RIDEV_NOLEGACY, // TODO: no legacy causes cam to move weirdly
			.hwndTarget		= hWnd
		},
		RAWINPUTDEVICE{
			.usUsagePage	= HID_USAGE_PAGE_GENERIC,
			.usUsage		= HID_USAGE_GENERIC_KEYBOARD,
			.dwFlags		= 0, // RIDEV_NOLEGACY, // NOTE: won't pass msgs like PtrSc
			.hwndTarget		= hWnd
		}
	};
	WIN_CHECK( RegisterRawInputDevices( hid, std::size( hid ), sizeof( RAWINPUTDEVICE ) ) );

    HtInitMemorySystem();

    persistentArena  = { g_pVirtualAllocator->AllocVirtualBlock( 2 * MB, 0 ) };
    pPersistentArena    = &persistentArena;

    gNumCores            = SysGetPhysicalCoreCount();

    threadCtxArray      = ArenaNewArray<thread_ctx>( persistentArena, gNumCores );
    for( thread_ctx& tctx : threadCtxArray )
    {
        tctx.scratchArenas = {
            g_pVirtualAllocator->AllocVirtualBlock( 2 * MB, 0 ),
            g_pVirtualAllocator->AllocVirtualBlock( 2 * MB, 0 )
        };
    }
    pThreadCtx	        = &threadCtxArray[ 0 ];

    // Init Job System
    pJobSys             = ArenaNew<job_system_ctx>( persistentArena );
    HT_ASSERT( nullptr != pJobSys );

    std::span threads   = ArenaNewArray<sys_thread>( persistentArena, gNumCores );
    for( u64 ti = 1; ti < std::size( threads ); ++ti )
    {
        fixed_wstring<16> name = { L"Thread #{}", ti };
        threads[ ti ] = SysCreateThread( 1 * MB, Win32ThreadLoop, ( void* ) ti, ( const wchar_t* ) name );
    }

	helltech_interface* pHelltech = MakeHelltech( persistentArena );

	pHelltech->Init( ( u64 ) hInst, ( u64 ) hWnd, SCREEN_WIDTH, SCREEN_HEIGHT );

	// NOTE: t0 = double( UINT64( 1ULL << 32 ) ) -> precision mostly const for the next ~136 years;
	// NOTE: double gives time precision of 1 uS
	bool			isRunning		= true;
	const double	ticksPerSecond  = 1.0 / double( SysGetCpuFreq() );
	//constexpr double	dt = 0.01;
	//double				t = double( UINT64( 1ULL << 32 ) );
	//double				accumulator = 0;
	u64				currentTicks	= SysTicks();

	while( isRunning )
	{
	    ht_mem_scope    memScope    = { pThreadCtx->scratchArenas[ 0 ] };

		const u64		newTicks	= SysTicks();
		const double	elapsedTime = double( newTicks - currentTicks ) * ticksPerSecond;
		currentTicks				= newTicks;
		//accumulator += elapsedTime;

		globalHtInputState			= HTReinitInputState( globalHtInputState );
		isRunning					= SysPumpUserInput();

		pHelltech->RunLoop( elapsedTime, isRunning, pThreadCtx->scratchArenas[ 0 ], globalHtInputState );
	}

	return 0;
}

