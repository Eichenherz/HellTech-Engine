#include "Win32/DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#pragma comment( lib, "Synchronization.lib" )
#include <windowsx.h>
#include <hidusage.h>

#include <iostream>
#include <algorithm>

#include <ranges>
#include <vector>
#include <string>

#include "ht_core_types.h"

#include <Win32/win32_err.h>
#include <System/sys_sync.h>
#include <System/sys_thread.h>

#include "engine_platform_common.h"
#include "System/Win32/win32_kbd_scancodes.h"

static inline void SysOsCreateConsole()
{
	WIN_CHECK( AllocConsole() );
	// NOTE: https://alexanderhoughton.co.uk/blog/redirect-all-stdout-stderr-to-console/
	//WIN_CHECK( !AttachConsole( GetCurrentProcessId() ) );
	HANDLE hConOut = CreateFileA(
		"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );
	WIN_CHECK( INVALID_HANDLE_VALUE != hConOut );
	WIN_CHECK( SetStdHandle( STD_OUTPUT_HANDLE, hConOut ) );
	WIN_CHECK( SetStdHandle( STD_ERROR_HANDLE, hConOut ) );

	WIN_CHECK( nullptr != freopen( "CONOUT$", "w", stdout ) );
	WIN_CHECK( nullptr != freopen( "CONOUT$", "w", stderr ) );

	std::ios::sync_with_stdio( true );
	std::cout.clear();
	std::cerr.clear();
	std::wcout.clear();
	std::wcerr.clear();
}

static inline u64 SysGetCpuFreq()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	return freq.QuadPart;
}
static inline u64 SysTicks()
{
	LARGE_INTEGER tick;
	QueryPerformanceCounter( &tick );
	return tick.QuadPart;
}

static inline bool SysPumpUserInput()
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

// TODO: add memory, interp mouse pos wrt frameTime
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
		default : break;
	}
	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}


using sys_physical_path = fixed_string<MAX_PATH>;


#include "ht_mem_arena.h"
#include "ht_stretchybuff.h"

// NOTE: global state
thread_local virtual_arena* pThisThreadArena	= nullptr;
job_system_ctx*				pJobSys				= nullptr;

constexpr u64 THREAD_ARENA_MAX_SIZE = 128 * MB;
//------------------=

struct sys_thread
{
	HANDLE				hndl;
	DWORD				threadId;
};

DWORD WINAPI Win32ThreadLoop( LPVOID lpParam )
{
	pThisThreadArena = new virtual_arena{ THREAD_ARENA_MAX_SIZE };

	for( ;; )
	{
		SysSemaphoreWait( pJobSys->sema, INFINITE );

		for( job_t job = {}; pJobSys->queue.TryPop( job ); )
		{
			job.PfnJob( job.payload, pThisThreadArena );
		}
	}

	return 0;
}


sys_thread SysCreateThread( u64	stackSize, const wchar_t* name )
{
	HT_ASSERT( nullptr != pJobSys );

	DWORD threadId = 0;
	HANDLE hThread = CreateThread( nullptr, stackSize, Win32ThreadLoop,
		nullptr, 0, &threadId );
	HT_ASSERT( INVALID_HANDLE_VALUE != hThread );

	SysNameThread( ( u64 ) hThread, name );

	return {
		.hndl		= hThread,
		.threadId	= threadId
	};
}

INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR, INT )
{
	using namespace DirectX;

	SysOsCreateConsole();

	SysNameThread( ( u64 ) GetCurrentThread(), L"Main Thread" );
#ifdef  _DEBUG
	// TODO: fix sys_path whatever to work with this !
	char workingDir[ MAX_PATH ] = {};
	WIN_CHECK( 0 != GetCurrentDirectoryA( std::size( workingDir ), workingDir ) );
	fixed_string<512> workingDirMsg = {"WorkingDir: {}\n", workingDir };
	std::cout << ( const char* ) workingDirMsg;
#endif //_DEBUG

	WIN_CHECK( DirectX::XMVerifyCPUSupport() );

	SYSTEM_INFO sysInfo = {};
	GetSystemInfo( &sysInfo );

	HT_ASSERT( OS_PAGE_SIZE_IN_BYTES == sysInfo.dwPageSize );

	WNDCLASSEX wc = {
		.cbSize			= sizeof( WNDCLASSEX ),
		.lpfnWndProc	= MainWndProc,
		.hInstance		= hInst,
		.hCursor		= LoadCursor( 0, IDC_ARROW ),
		.lpszClassName	= ENGINE_NAME
	};
	WIN_CHECK( RegisterClassExA( &wc ) );

	LONG left = 350;
	LONG top = 100;

	RECT wr = { .left = left, .top = top, .right = ( LONG ) SCREEN_WIDTH + left, .bottom = ( LONG ) SCREEN_HEIGHT + top };

	constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRect( &wr, windowStyle, 0 );
	HWND hWnd = CreateWindow( wc.lpszClassName, WINDOW_TITLE, windowStyle, wr.left, wr.top,
		wr.right - wr.left, wr.bottom - wr.top, 0, 0, hInst, 0 );
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

	pThisThreadArena = new virtual_arena{ THREAD_ARENA_MAX_SIZE };
	ht_virtual_allocator* virtAlloc = new ht_virtual_allocator{ 1 * GB };
	u8* alignedAlloc = virtAlloc->Alloc( 10 * MB, 64 );
	std::memset( alignedAlloc, 0, 10 * MB );
	virtAlloc->Free( alignedAlloc );
	// NOTE: init Job System
	pJobSys = new job_system_ctx{};
	std::vector<sys_thread> threads;
	threads.push_back( SysCreateThread( 1 * MB,  L"IO Thread" ) );
	// ----------------------------------------------------------

	HT_ASSERT( nullptr != pJobSys );

	helltech_interface* pHelltech = MakeHelltech();

	pHelltech->Init( ( u64 ) hInst, ( u64 ) hWnd, SCREEN_WIDTH, SCREEN_HEIGHT );

	// NOTE: time is a double of seconds
	// NOTE: t0 = double( UINT64( 1ULL << 32 ) ) -> precision mostly const for the next ~136 years;
	// NOTE: double gives time precision of 1 uS
	bool			isRunning = true;
	const double	cpuPeriod = 1.0 / double( SysGetCpuFreq() );
	//constexpr double	dt = 0.01;
	//double				t = double( UINT64( 1ULL << 32 ) );
	//double				accumulator = 0;
	u64				currentTicks = SysTicks();

	while( isRunning )
	{
		const u64 newTicks = SysTicks();
		const double elapsedTime = double( newTicks - currentTicks ) * cpuPeriod;
		currentTicks = newTicks;
		//accumulator += elapsedTime;

		globalHtInputState = HTReinitInputState( globalHtInputState );
		isRunning = SysPumpUserInput();

		pHelltech->RunLoop( elapsedTime, isRunning, *pThisThreadArena, globalHtInputState );
	}

	return 0;
}

