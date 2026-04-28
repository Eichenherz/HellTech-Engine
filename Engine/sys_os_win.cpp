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

static void Win32ProcessRawInput( const RAWINPUT& ri, input_state& inputState )
{
	if( RIM_TYPEKEYBOARD == ri.header.dwType )
	{
		const RAWKEYBOARD& kb = ri.data.keyboard;
		if( KEYBOARD_OVERRUN_MAKE_CODE == kb.MakeCode ) return;

		bool isPressed = !( kb.Flags & RI_KEY_BREAK );
		bool isE0 = kb.Flags & RI_KEY_E0;
		u16 keyIndex = ( u16 ) ( kb.MakeCode | ( isE0 ? 0x100 : 0 ) );
		inputState.keyStates[ keyIndex ] = isPressed;
	}
	if( RIM_TYPEMOUSE == ri.header.dwType )
	{
		if( !( ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) )
		{
			inputState.mouseDx += ri.data.mouse.lLastX;
			inputState.mouseDy += ri.data.mouse.lLastY;
		}

		USHORT usButtonFlags =  ri.data.mouse.usButtonFlags;
		if( usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN )   inputState.mouseButtons[ 0 ] = 1;
		if( usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP )     inputState.mouseButtons[ 0 ] = 0;
		if( usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN )  inputState.mouseButtons[ 1 ] = 1;
		if( usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP )    inputState.mouseButtons[ 1 ] = 0;
		if( usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN ) inputState.mouseButtons[ 2 ] = 1;
		if( usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP )   inputState.mouseButtons[ 2 ] = 0;
	}
}

LRESULT CALLBACK MainWndProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch( uMsg )
	{
	case WM_NCCREATE:
	{
		CREATESTRUCTW* cs = ( CREATESTRUCTW* ) lParam;
		SetWindowLongPtr( hwnd, GWLP_USERDATA, ( LONG_PTR ) cs->lpCreateParams );
		return TRUE;
	}
	// TODO: this will exit the Loop immediately. 
	case WM_CLOSE: case WM_DESTROY:  PostQuitMessage( 0 ); return 0; // NOTE: no more def handling 
	case WM_MOUSEMOVE:
	{
		LONG_PTR pUserData = GetWindowLongPtr( hwnd, GWLP_USERDATA );
		if( pUserData )
		{
			input_state& inputState = *( input_state* ) pUserData;
			inputState.mouseX = ( float ) GET_X_LPARAM( lParam );
			inputState.mouseY = ( float ) GET_Y_LPARAM( lParam );
		}
		break;
	}
		
	case WM_INPUT:
	{
		constexpr u64 cbSzHeader = sizeof( RAWINPUTHEADER );
		HRAWINPUT hri = ( HRAWINPUT ) lParam;

		// TODO: self supply this
		static thread_local std::vector<u8> scratchPad;

		UINT size = 0;
		if( GetRawInputData( hri, RID_INPUT, nullptr, &size, cbSzHeader ) == UINT( -1 ) || !size  )
		{
			return 0;
		}

		scratchPad.resize( size );
		if( GetRawInputData( hri, RID_INPUT, std::data( scratchPad ), &size, cbSzHeader ) == UINT( -1 ) )
		{
			return 0;
		}

		const RAWINPUT& ri = *( const RAWINPUT* ) std::data( scratchPad );

		LONG_PTR pUserData = GetWindowLongPtr( hwnd, GWLP_USERDATA );
		if( !pUserData ) return 0;

		Win32ProcessRawInput( ri, *( input_state* ) pUserData );

		return 0;
	}
	}
	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}


using sys_physical_path = fixed_string<MAX_PATH>;


#include "ht_mem_arena.h"
#include "ht_stretchybuff.h"



struct sys_thread_data
{
	virtual_arena		arena;
	job_system_ctx*		jobSysCtx;
};

struct sys_thread
{
	HANDLE				hndl;
	sys_thread_data*	pData;
	DWORD				threadId;
};

DWORD WINAPI Win32ThreadLoop( LPVOID lpParam )
{
	sys_thread_data& threadCtx = *( sys_thread_data* ) lpParam;

	for( ;; )
	{
		SysSemaphoreWait( threadCtx.jobSysCtx->sema, INFINITE );

		for( job_t job = {}; threadCtx.jobSysCtx->queue.TryPop( job ); )
		{
			job.PfnJob( job.payload, &threadCtx.arena );
		}
	}

	return 0;
}


sys_thread SysCreateThread(
	u64									stackSize,
	u64									maxScratchPadSize,
	const wchar_t*						name,
	job_system_ctx*						pJobSys,
	ht_stretchybuff<sys_thread_data>&	threadDataBuff
) {
	HT_ASSERT( nullptr != pJobSys );

	sys_thread_data* pData = &threadDataBuff.push_back( {
		.arena		= virtual_arena{ maxScratchPadSize },
		.jobSysCtx	= pJobSys,
	} );

	DWORD threadId = 0;
	HANDLE hThread = CreateThread( NULL, stackSize, Win32ThreadLoop, ( LPVOID ) pData, 0, &threadId );
	HT_ASSERT( INVALID_HANDLE_VALUE != hThread );

	SysNameThread( ( u64 ) hThread, name );

	return {
		.hndl		= hThread,
		.pData		= pData,
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

	input_state inputState = {};

	constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRect( &wr, windowStyle, 0 );
	HWND hWnd = CreateWindow( wc.lpszClassName, WINDOW_TITLE, windowStyle, wr.left, wr.top,
		wr.right - wr.left, wr.bottom - wr.top, 0, 0, hInst, &inputState );
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

	// TODO: mem system
	virtual_arena platformArena = { 10 * MB };
	virtual_arena scratchArena = { 10 * MB };

	job_system_ctx* jobSystemCtx = ArenaNew<job_system_ctx>( platformArena );
	ht_stretchybuff<sys_thread_data> threadDataBuff  = HtANewStretchyBuffFromArena<sys_thread_data>(
		platformArena, NUM_CORES );
	ht_stretchybuff<sys_thread> threads = HtANewStretchyBuffFromArena<sys_thread>( platformArena, NUM_CORES );
	threads.push_back( SysCreateThread( 1 * MB, 1 * GB, L"IO Thread", jobSystemCtx, threadDataBuff ) );

	helltech_interface* pHelltech = MakeHelltech( platformArena );

	pHelltech->Init( jobSystemCtx, ( u64 ) hInst, ( u64 ) hWnd, SCREEN_WIDTH, SCREEN_HEIGHT );

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

		// NOTE: reset mouse dMove
		inputState.mouseDx = 0;
		inputState.mouseDy = 0;

		isRunning = SysPumpUserInput();

		pHelltech->RunLoop( elapsedTime, isRunning, scratchArena, inputState );
	}

	return 0;
}

