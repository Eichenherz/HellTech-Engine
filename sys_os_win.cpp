#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#include <hidusage.h>
#include <shlwapi.h>
#include <fileapi.h>
#include <errhandlingapi.h>
#include <strsafe.h>

#include <assert.h>
#include <stdlib.h>

#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <charconv>
#include <string>
#include <algorithm>

#include <EASTL/internal/config.h>

using namespace std;

#include "sys_os_api.h"
#include "core_types.h"

#include "r_data_structs.h"

#include <System/Win32/win32_err.h>

static inline void SysOsCreateConsole()
{
	WIN_CHECK( !AllocConsole() );
	// NOTE: https://alexanderhoughton.co.uk/blog/redirect-all-stdout-stderr-to-console/
	//WIN_CHECK( !AttachConsole( GetCurrentProcessId() ) );
	HANDLE hConOut = CreateFileA(
		"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );
	WIN_CHECK( INVALID_HANDLE_VALUE == hConOut );
	WIN_CHECK( !SetStdHandle( STD_OUTPUT_HANDLE, hConOut ) );
	WIN_CHECK( !SetStdHandle( STD_ERROR_HANDLE, hConOut ) );

	//Ensures FILE* object remapped to new console window
	freopen("CONOUT$", "w", stdout );
	freopen("CONOUT$", "w", stderr );

	//Allows use of _write(1,...) & _write(2,...) to be redirected
	freopen("CONOUT$", "w", _fdopen(1, "w" ) );
	freopen("CONOUT$", "w", _fdopen(2, "w" ) );

	std::wcout.clear();
	std::cout.clear();
	std::wcerr.clear();
	std::cerr.clear();
}
static inline void SysOsKillConsole()
{
	FreeConsole();
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

// PLATOFRM_FILE_API
// TODO: use own memory system 
// TODO: multithreading
// TODO: async
// TODO: streaming
// TODO: better file api
// TODO: char vs wchar
static inline u32 SysGetFileAbsPath( const char* fileName, char* buffer, u64 buffSize )
{
	static_assert( sizeof( DWORD ) == sizeof( u32 ) );
	return GetFullPathNameA( fileName, buffSize, buffer, 0 );
}
static inline HANDLE WinGetReadOnlyFileHandle( const char* fileName )
{
	DWORD accessMode = GENERIC_READ;
	DWORD shareMode = 0;
	DWORD creationDisp = OPEN_EXISTING;
	DWORD flagsAndAttrs = FILE_ATTRIBUTE_READONLY;
	return CreateFile( fileName, accessMode, shareMode, 0, creationDisp, flagsAndAttrs, 0 );
}
inline std::vector<u8> SysReadFile( const char* fileName )
{
	HANDLE hfile = WinGetReadOnlyFileHandle( fileName );
	if( hfile == INVALID_HANDLE_VALUE ) return{};

	LARGE_INTEGER fileSize = {};
	WIN_CHECK( !GetFileSizeEx( hfile, &fileSize ) );

	std::vector<u8> fileData( fileSize.QuadPart );

	OVERLAPPED asyncIo = {};
	WIN_CHECK( !ReadFileEx( hfile, std::data( fileData ), fileSize.QuadPart, &asyncIo, 0 ) );
	CloseHandle( hfile );

	return fileData;
}
// TODO: can be any kind of handle
inline u64 SysGetFileTimestamp( const char* filename )
{
	HANDLE hfile = WinGetReadOnlyFileHandle( filename );
	FILETIME fileTime = {};
	WIN_CHECK( !GetFileTime( hfile, 0, 0, &fileTime ) );

	ULARGE_INTEGER timestamp = {};
	timestamp.LowPart = fileTime.dwLowDateTime;
	timestamp.HighPart = fileTime.dwHighDateTime;

	CloseHandle( hfile );

	return u64( timestamp.QuadPart );
}
// TODO: might not want to crash when file can't be written/read
inline bool SysWriteToFile( const char* filename, const u8* data, u64 sizeInBytes )
{
	DWORD accessMode = GENERIC_WRITE;
	DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	DWORD creationDisp = OPEN_ALWAYS;
	DWORD flagsAndAttrs = FILE_ATTRIBUTE_NORMAL;
	HANDLE hFile = CreateFile( filename, accessMode, shareMode, 0, creationDisp, flagsAndAttrs, 0 );

	OVERLAPPED asyncIo = {};
	WIN_CHECK( !WriteFileEx( hFile, data, sizeInBytes, &asyncIo, 0 ) );
	CloseHandle( hFile );

	return true;
}

constexpr char	ENGINE_NAME[] = "helltech_engine";
constexpr char	WINDOW_TITLE[] = "HellTech Engine";

constexpr u16 VK_W = 0x57;
constexpr u16 VK_A = 0x41;
constexpr u16 VK_S = 0x53;
constexpr u16 VK_D = 0x44;
constexpr u16 VK_C = 0x43;
constexpr u16 VK_F = 0x46;
constexpr u16 VK_O = 0x4F;

constexpr float ALMOST_HALF_PI = 0.995f * DirectX::XM_PIDIV2;

struct virtual_camera
{
	DirectX::XMFLOAT4X4A projection;
	DirectX::XMFLOAT4X4A prevViewProj = {};

	DirectX::XMFLOAT3 worldPos = { 0.0f, 0.0f, 0.0f };

	static constexpr DirectX::XMFLOAT3 fwdBasis = { 0.0f, 0.0f, 1.0f };
	static constexpr DirectX::XMFLOAT3 upBasis = { 0.0f, 1.0f, 0.0f };
	// NOTE: pitch must be in [-pi/2,pi/2]
	float pitch = 0.0f;
	float yaw = 0.0f;

	float speed = 1.5f;

	inline virtual_camera( DirectX::XMMATRIX proj )
	{
		DirectX::XMStoreFloat4x4A( &projection, proj );
	}

	inline void XM_CALLCONV Move( DirectX::XMVECTOR camMove, DirectX::XMFLOAT2 dRot, float elapsedSecs )
	{
		using namespace DirectX;

		yaw = XMScalarModAngle( yaw + dRot.x * elapsedSecs );
		pitch = std::clamp( pitch + dRot.y * elapsedSecs, -ALMOST_HALF_PI, ALMOST_HALF_PI );

		XMMATRIX tRotScale = XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) * XMMatrixScaling( speed, speed, speed );
		XMVECTOR xmCamMove = XMVector3Transform( XMVector3Normalize( camMove ), tRotScale );

		XMVECTOR xmCamPos = XMLoadFloat3( &worldPos );
		XMVECTOR smoothNewCamPos = XMVectorLerp( xmCamPos, XMVectorAdd( xmCamPos, xmCamMove ), 0.18f * elapsedSecs / 0.0166f );

		// TODO: thresholds
		//float moveLen = XMVectorGetX( XMVector3Length( smoothNewCamPos ) );
		XMStoreFloat3( &worldPos, smoothNewCamPos );
	}

	inline view_data GetViewData() const
	{
		using namespace DirectX;

		XMVECTOR xmFwd = XMLoadFloat3( &fwdBasis );
		XMVECTOR xmUp = XMLoadFloat3( &upBasis );
		XMVECTOR xmWorldPos = XMLoadFloat3( &worldPos );

		XMVECTOR camLookAt = XMVector3Transform( xmFwd, XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
		XMMATRIX view = XMMatrixLookAtLH( xmWorldPos, XMVectorAdd( xmWorldPos, camLookAt ), xmUp );

		XMVECTOR viewDet = XMMatrixDeterminant( view );
		XMMATRIX invView = XMMatrixInverse( &viewDet, view );
		XMVECTOR viewDir = XMVectorNegate( invView.r[ 2 ] );

		XMMATRIX xmProj = XMLoadFloat4x4A( &projection );

		view_data viewData = {};
		viewData.proj = projection;
		XMStoreFloat4x4A( &viewData.mainView, view );
		XMStoreFloat4x4A( &viewData.mainViewProj, XMMatrixMultiply( view, xmProj ) );
		viewData.prevViewProj = prevViewProj;
		viewData.worldPos = worldPos;
		XMStoreFloat3( &viewData.camViewDir, viewDir );

		return viewData;
	}
};

// NOTE: this is useful when we want to draw the frozen frustum
inline DirectX::XMMATRIX XM_CALLCONV FrustumMatrixFromViewProj( DirectX::XMMATRIX viewXProj )
{
	using namespace DirectX;

	// NOTE: inv( A * B ) = inv B * inv A
	XMMATRIX invFrustMat = viewXProj; //XMMatrixMultiply( view, proj );
	XMVECTOR det = XMMatrixDeterminant( invFrustMat );
	assert( XMVectorGetX( det ) );
	XMMATRIX frustMat = XMMatrixInverse( &det, invFrustMat );
	return frustMat;
}

// TODO: templates ? 
// TODO: fromalize world coord
inline DirectX::XMMATRIX PerspRevInfFovLH( float fovYRads, float aspectRatioWH, float zNear )
{
	float sinFov;
	float cosFov;

	DirectX::XMScalarSinCos( &sinFov, &cosFov, fovYRads * 0.5f );

	float h = cosFov / sinFov;
	float w = h / aspectRatioWH;

	DirectX::XMMATRIX proj;
	proj.r[ 0 ] = DirectX::XMVectorSet( w, 0, 0, 0 );
	proj.r[ 1 ] = DirectX::XMVectorSet( 0, h, 0, 0 );
	proj.r[ 2 ] = DirectX::XMVectorSet( 0, 0, 0, zNear );
	proj.r[ 3 ] = DirectX::XMVectorSet( 0, 0, 1, 0 );

	return proj;
}
inline DirectX::XMMATRIX PerspRevInfFovRH( float fovYRads, float aspectRatioWH, float zNear )
{
	float sinFov;
	float cosFov;

	DirectX::XMScalarSinCos( &sinFov, &cosFov, fovYRads * 0.5f );

	float h = cosFov / sinFov;
	float w = h / aspectRatioWH;

	DirectX::XMMATRIX proj;
	proj.r[ 0 ] = DirectX::XMVectorSet( w, 0, 0, 0 );
	proj.r[ 1 ] = DirectX::XMVectorSet( 0, h, 0, 0 );
	proj.r[ 2 ] = DirectX::XMVectorSet( 0, 0, 0, zNear );
	proj.r[ 3 ] = DirectX::XMVectorSet( 0, 0, -1, 0 );

	return proj;
}

inline u64	SysDllLoad( const char* name )
{
	return (u64) LoadLibrary( name );
}
inline void	SysDllUnload( u64 hDll )
{
	if( !hDll ) return;
	WIN_CHECK( !FreeLibrary( (HMODULE) hDll ) );
}
inline void*	SysGetProcAddr( u64 hDll, const char* procName )
{
	return (void*)GetProcAddress( (HMODULE) hDll, procName );
}

static inline u64	SysGetCpuFreq()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	return freq.QuadPart;
}
static inline u64	SysTicks()
{
	LARGE_INTEGER tick;
	QueryPerformanceCounter( &tick );
	return double( tick.QuadPart );
}

struct mouse
{
	float dx, dy;
};

struct keyboard
{
	bool w, a, s, d;
	bool c;
	bool space, lctrl;
	bool f, o;
	bool esc;
};

struct alignas( 4 ) input_state
{
	vec2 dMouse;
	u8 keyStates[ 512 ];
};

struct move_cam_action
{
	DirectX::XMVECTOR camMove;
	DirectX::XMFLOAT2 dRot;
};

inline move_cam_action GetMoveCamAction( const input_state& inputState )
{
	using namespace DirectX;

	XMVECTOR camMove = XMVectorSet( 0, 0, 0, 0 );
	if( inputState.keyStates[ VK_W ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, 1, 0 ) );
	if( inputState.keyStates[ VK_A ] ) camMove = XMVectorAdd( camMove, XMVectorSet( -1, 0, 0, 0 ) );
	if( inputState.keyStates[ VK_S ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, -1, 0 ) );
	if( inputState.keyStates[ VK_D ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 1, 0, 0, 0 ) );
	if( inputState.keyStates[ VK_SPACE ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 1, 0, 0 ) );
	if( inputState.keyStates[ VK_C ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, -1, 0, 0 ) );

	if( !XMVector3Equal( camMove, XMVectorZero() ) )
	{
		camMove = XMVector3Normalize( camMove );
	}

	return { .camMove = camMove, .dRot = inputState.dMouse };
}

static inline bool	SysPumpUserInput()
{
	MSG msg;
	while( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ) )
	{
		TranslateMessage( &msg );
		DispatchMessageA( &msg );
		if( msg.message == WM_QUIT ) return false;
	}

	return true;
}

static inline u16 KeyIndex( const RAWKEYBOARD& kb )
{
	bool isE0 = kb.Flags & RI_KEY_E0;
	return ( u16 ) ( kb.MakeCode | ( isE0 ? 0x100 : 0 ) );
}

static void Win32ProcessRawInput( const RAWINPUT& ri, input_state& inputState )
{
	if( ri.header.dwType == RIM_TYPEKEYBOARD )
	{
		bool isPressed = !( ri.data.keyboard.Flags & RI_KEY_BREAK );
		inputState.keyStates[ KeyIndex( ri.data.keyboard ) ] = isPressed;
	}
	if( ( ri.header.dwType == RIM_TYPEMOUSE ) )
	{
		inputState.dMouse = { ( float ) ri.data.mouse.lLastX, ( float ) ri.data.mouse.lLastY };
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
		
	case WM_INPUT:
	{
		constexpr u64 cbSzHeader = sizeof( RAWINPUTHEADER );
		HRAWINPUT hri = ( HRAWINPUT ) lParam;

		// TODO: might wanna self supply this
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

#include "imgui/imgui.h"

static void EastlAssertFail( const char* expr, void* )
{
	HtPrintErrAndDie( "{}", expr );
}

INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR, INT )
{
	using namespace DirectX;

	input_state inputState = {};

	eastl::SetAssertionFailureFunction(&EastlAssertFail, nullptr);
	WIN_CHECK( !DirectX::XMVerifyCPUSupport() );

	SysOsCreateConsole();

	SYSTEM_INFO sysInfo = {};
	GetSystemInfo( &sysInfo );
	
	WNDCLASSEX wc = {
		.cbSize = sizeof( WNDCLASSEX ),
		.lpfnWndProc = MainWndProc,
		.hInstance = hInst,
		.hCursor = LoadCursor( 0, IDC_ARROW ),
		.lpszClassName = ENGINE_NAME
	};
	WIN_CHECK( !RegisterClassEx( &wc ) );
	
	RECT wr = {
		.left = 350,
		.top = 100,
		.right = ( LONG ) SCREEN_WIDTH + wr.left,
		.bottom = ( LONG ) SCREEN_HEIGHT + wr.top
	};

	constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRect( &wr, windowStyle, 0 );
	HWND hWnd = CreateWindow( 
		wc.lpszClassName, WINDOW_TITLE, windowStyle, wr.left,wr.top, wr.right - wr.left, wr.bottom - wr.top, 0,0, hInst, &inputState );
	WIN_CHECK(  !hWnd );

	ShowWindow( hWnd, SW_SHOWDEFAULT );

	// NOTE: don't use RIDEV_INPUTSINK in order to only receive when in focus
	RAWINPUTDEVICE hid[ 2 ] = {};
	hid[ 0 ].usUsage = HID_USAGE_GENERIC_MOUSE;
	hid[ 0 ].usUsagePage = HID_USAGE_PAGE_GENERIC;
	hid[ 0 ].hwndTarget = hWnd;
	// TODO: no legacy causes cam to move weirdly
	hid[ 0 ].dwFlags = 0;// RIDEV_NOLEGACY;

	hid[ 1 ].usUsage = HID_USAGE_GENERIC_KEYBOARD;
	hid[ 1 ].usUsagePage = HID_USAGE_PAGE_GENERIC;
	hid[ 1 ].hwndTarget = hWnd;
	// NOTE: won't pass msgs like PtrSc
	hid[ 1 ].dwFlags = 0;// RIDEV_NOLEGACY;
	WIN_CHECK( !RegisterRawInputDevices( hid, std::size( hid ), sizeof( RAWINPUTDEVICE ) ) );

	keyboard kbd = {};

	constexpr float almostPiDiv2 = 0.995f * DirectX::XM_PIDIV2;
	float mouseSensitivity = 0.1f;
	float camSpeed = 1.5f;
	float moveThreshold = 0.0001f;

	constexpr float zNear = 0.5f;
	XMMATRIX proj = PerspRevInfFovLH( XMConvertToRadians( 70.0f ), float( SCREEN_WIDTH ) / float( SCREEN_HEIGHT ), zNear );

	virtual_camera mainActiveCam = { proj };
	virtual_camera debugCam = { proj };

	{
		view_data mainViewData = mainActiveCam.GetViewData();
		mainActiveCam.prevViewProj = mainViewData.mainViewProj;
	}

	gpu_data gpuData = {};
	frame_data frameData = {};

	frameData.views.resize( 2 );
	u16 mainViewIdx = 0;
	u16 dbgViewIdx = 1;

	VkBackendInit( ( uintptr_t ) hInst, ( uintptr_t ) hWnd );

	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = { SCREEN_WIDTH,SCREEN_HEIGHT };
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	

	// NOTE: time is a double of seconds
	// NOTE: t0 = double( UINT64( 1ULL << 32 ) ) -> precision mostly const for the next ~136 years;
	// NOTE: double gives time precision of 1 uS
	bool				isRunning = true;
	const double		cpuPeriod = 1.0 / double( SysGetCpuFreq() );
	//constexpr double	dt = 0.01;
	//double				t = double( UINT64( 1ULL << 32 ) );
	//double				accumulator = 0;
	u64					currentTicks = SysTicks();

	// TODO: QUIT immediately ?
	while( isRunning )
	{
		const u64 newTicks = SysTicks();
		const double elapsedSecs = double( newTicks - currentTicks ) * cpuPeriod;
		currentTicks = newTicks;
		//accumulator += elapsedSecs;

		inputState.dMouse = {};

		isRunning = SysPumpUserInput();

		auto[ camMove, dRot ] = GetMoveCamAction( inputState );

		mainActiveCam.Move( camMove, dRot, elapsedSecs );
		debugCam.Move( camMove, dRot, elapsedSecs );

		if( !kbd.f )
		{
			view_data mainViewData = mainActiveCam.GetViewData();
			frameData.views[ mainViewIdx ] = mainViewData;

			mainActiveCam.prevViewProj = mainViewData.mainViewProj;
		}
		
		frameData.views[ dbgViewIdx ] = debugCam.GetViewData();

		XMMATRIX frustMat = FrustumMatrixFromViewProj( XMLoadFloat4x4A( &frameData.views[ mainViewIdx ].mainViewProj ) );
		XMStoreFloat4x4A( &frameData.frustTransf, frustMat );

		frameData.elapsedSeconds = elapsedSecs;
		frameData.freezeMainView = kbd.f;
		frameData.dbgDraw = kbd.o;

		ImGui::NewFrame();
		std::string wndMsg( std::to_string( gpuData.timeMs ) );
		
		ImGui::SetNextWindowPos( {} );
		ImGui::SetNextWindowSize( { std::size( wndMsg ) * ImGui::GetFontSize(),50 } );
		constexpr ImGuiWindowFlags wndFlag = 
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
		ImGui::Begin( "GPU ms:", 0, wndFlag );
		ImGui::Text( wndMsg.c_str() );
		ImGui::End();

		ImGui::Render();
		ImGui::EndFrame();

		HostFrames( frameData, gpuData );
	}

	VkBackendKill();
	SysOsKillConsole();

	return 0;
}

