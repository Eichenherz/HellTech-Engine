#include <System/Win32/DEFS_WIN32_NO_BS.h>
#include <Windows.h>
#pragma comment( lib, "Synchronization.lib" )
#include <windowsx.h>
#include <hidusage.h>

#include <algorithm>

#include <vector>

#include <ht_core_types.h>

#include <System/Win32/win32_err.h>
#include <System/sys_sync.h>
#include <System/sys_thread.h>
#include <System/sys_timer.h>
#include <System/sys_std_streams.h>

#include "engine_platform_common.h"
#include <System/Win32/win32_kbd_scancodes.h>

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

		bool isE0 = kb.Flags & RI_KEY_E0;
		u16 keyIndex = ( u16 ) ( kb.MakeCode | ( isE0 ? 0x100 : 0 ) );
		bool isPressed = !( kb.Flags & RI_KEY_BREAK );
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
			globalHtInputState.mousePos = { ( float ) GET_X_LPARAM( lParam ), ( float ) GET_Y_LPARAM( lParam ) };
			break;
		}
		
		case WM_INPUT:
		{
			HRAWINPUT hri = ( HRAWINPUT ) lParam;

			// TODO: self supply this
			thread_local std::vector<u8> scratchPad;

			UINT size = 0;
			if( GetRawInputData( hri, RID_INPUT, nullptr, &size,
				sizeof( RAWINPUTHEADER ) ) == UINT( -1 ) || !size  )
			{
				break;
			}

			scratchPad.resize( size );
			if( GetRawInputData( hri, RID_INPUT, std::data( scratchPad ), &size,
				sizeof( RAWINPUTHEADER ) ) == UINT( -1 ) )
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


#include <ht_memory.h>


// NOTE: global state
static job_system_ctx*				pJobSys		= nullptr;
static thread_local dynamic_arena	threadArena	= {};
//------------------=


UINT WINAPI Win32ThreadLoop( LPVOID lpParam )
{
	g_pThisThreadHeap	= HtGetThreadHeap( ( u64 ) lpParam );
	threadArena		= g_pThisThreadHeap->Allocate( 1 * MB );

	for( ;; )
	{
		SysSemaphoreWait( pJobSys->sema, INFINITE );

		for( job_t job = {}; pJobSys->queue.TryPop( job ); )
		{
			job.PfnJob( job.payload, &threadArena );
		}
	}

	return 0;
}

INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR, INT )
{
	SysOsCreateConsole();

	SysNameThread( ( u64 ) GetCurrentThread(), L"Main Thread" );

#ifdef  _DEBUG
	// TODO: fix sys_path whatever to work with this !
	char workingDir[ MAX_PATH ] = {};
	WIN_CHECK( 0 != GetCurrentDirectoryA( std::size( workingDir ), workingDir ) );
	fixed_string<512> workingDirMsg = { "WorkingDir: {}\n", workingDir };
	SysWriteToStdStream( ( const char* ) workingDirMsg, sys_stream_t::OUTPUT );
#endif //_DEBUG

	WIN_CHECK( DirectX::XMVerifyCPUSupport() );

	SYSTEM_INFO sysInfo = {};
	GetSystemInfo( &sysInfo );

	HT_ASSERT( OS_PAGE_SIZE_IN_BYTES == sysInfo.dwPageSize );
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

	LONG left	= 200;
	LONG top	= 100;
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
	RAWINPUTDEVICE hid[ 2 ] = {
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

	constexpr u64 NUM_CORES = 8;

	HtMakeAllocator( 32 * GB, NUM_CORES );
	g_pThisThreadHeap	= HtGetThreadHeap( 0 );
	threadArena			= g_pThisThreadHeap->Allocate( 1 * MB );

	// NOTE: init Job System
	pJobSys = ArenaNew<job_system_ctx>( threadArena );
	fixed_vector<sys_thread, NUM_CORES> threads;
	for( u64 ti = 0; ti < NUM_CORES - 1; ti++ )
	{
		fixed_wstring<16> name = { L"Thread #{}", ti + 1 };
		threads.push_back( SysCreateThread(
			1 * MB, Win32ThreadLoop, ( void* ) ( ti + 1 ), ( const wchar_t* ) name ) );
	}
	// ----------------------------------------------------------

	HT_ASSERT( nullptr != pJobSys );

	helltech_interface* pHelltech = MakeHelltech( threadArena );

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
		const u64		newTicks	= SysTicks();
		const double	elapsedTime = double( newTicks - currentTicks ) * ticksPerSecond;
		currentTicks				= newTicks;
		//accumulator += elapsedTime;

		globalHtInputState			= HTReinitInputState( globalHtInputState );
		isRunning					= SysPumpUserInput();

		pHelltech->RunLoop( elapsedTime, isRunning, threadArena, globalHtInputState );
	}

	return 0;
}

