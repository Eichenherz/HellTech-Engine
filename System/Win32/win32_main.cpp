#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#include <hidusage.h>
#include <iostream>
#include <io.h>
#include <shlwapi.h>
#include <fcntl.h>
#include "win32_utils.hpp"

#include <memory>

#include <System/sys_platform.hpp>

#include "helltech_config.hpp"

// TODO: handle debug func better
// TODO: all streams to console  
// TODO: customize console  
static inline void SysBindIOStreamToConsole()
{
	// https://stackoverflow.com/questions/311955/redirecting-cout-to-a-console-in-windows
	// Re-initialize the C runtime "FILE" handles with clean handles bound to "nul". We do this because it has been
	// observed that the file number of our standard handle file objects can be assigned internally to a value of -2
	// when not bound to a valid target, which represents some kind of unknown internal invalid state. In this state our
	// call to "_dup2" fails, as it specifically tests to ensure that the target file number isn't equal to this value
	// before allowing the operation to continue. We can resolve this issue by first "re-opening" the target files to
	// use the "nul" device, which will place them into a valid state, after which we can redirect them to our target
	// using the "_dup2" function.

	FILE* dummyFile;
	freopen_s( &dummyFile, "nul", "w", stdout );

	HANDLE hStd = GetStdHandle( STD_OUTPUT_HANDLE );
	i32 fileDescriptor = _open_osfhandle( (i64) hStd, _O_TEXT );
	FILE* file = _fdopen( fileDescriptor, "r" );
	i32 dup2Res = _dup2( _fileno( file ), _fileno( stdout ) );

	WIN_CHECK( !( dup2Res != -1 ) );

	setvbuf( stdout, 0, _IONBF, 0 );


	// Clear the error state for each of the C++ standard stream objects. We need to do this, as attempts to access the
	// standard streams before they refer to a valid target will cause the iostream objects to enter an error state. In
	// versions of Visual Studio after 2005, this seems to always occur during startup regardless of whether anything
	// has been read from or written to the targets or not.
	std::wcout.clear();
	std::cout.clear();
}

struct win32_console
{
	inline win32_console()
	{
		WIN_CHECK( !AllocConsole() );
		SysBindIOStreamToConsole();
	}

	inline ~win32_console()
	{
		FreeConsole();
	}
};

static LRESULT CALLBACK MainWndProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch( uMsg )
	{
	case WM_CLOSE: case WM_DESTROY:  PostQuitMessage( 0 ); break;
	}
	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}	

static INT WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, INT )
{
	using namespace DirectX;

	WIN_CHECK( !DirectX::XMVerifyCPUSupport() );

	SYSTEM_INFO	sysInfo = {};
	GetSystemInfo( &sysInfo );

	std::unique_ptr<win32_console> pWin32Console = std::make_unique<win32_console>();

	HINSTANCE hInst = hInstance;
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
		.right = SCREEN_WIDTH + wr.left,
		.bottom = SCREEN_HEIGHT + wr.top
	};
	constexpr DWORD windowStyle = WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU;
	AdjustWindowRect( &wr, windowStyle, 0 );
	HWND hWnd = CreateWindow(
		wc.lpszClassName, WINDOW_TITLE, windowStyle, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, 0, 0, hInst, 0 );
	WIN_CHECK(  !hWnd );

	ShowWindow( hWnd, SW_SHOWDEFAULT );


	RAWINPUTDEVICE hid[ 2 ] = {
		{
			.usUsagePage = HID_USAGE_PAGE_GENERIC,
			.usUsage = HID_USAGE_GENERIC_MOUSE,
			// TODO: no legacy causes cam to move weirdly
			.dwFlags = 0,// RIDEV_NOLEGACY;
			.hwndTarget = hWnd
		},
		{
			.usUsagePage = HID_USAGE_PAGE_GENERIC,
			.usUsage = HID_USAGE_GENERIC_KEYBOARD,
			// NOTE: won't pass msgs like PtrSc
			.dwFlags = 0, // RIDEV_NOLEGACY;
			.hwndTarget = hWnd
		}
	};
	WIN_CHECK( !RegisterRawInputDevices( hid, std::size( hid ), sizeof( RAWINPUTDEVICE ) ) );


	mouse m = {};
	keyboard kbd = {};

	constexpr float almostPiDiv2 = 0.995f * DirectX::XM_PIDIV2;
	float mouseSensitivity = 0.1f;
	float camSpeed = 1.5f;
	float moveThreshold = 0.0001f;

	constexpr float zNear = 0.5f;
	XMMATRIX proj = PerspRevInfFovLH( XMConvertToRadians( 70.0f ), float( SCREEN_WIDTH ) / float( SCREEN_HEIGHT ), zNear );
	// NOTE: pitch must be in [-pi/2,pi/2]
	float pitch = 0;
	float yaw = 0;

	XMVECTOR camFwdBasis = XMVectorSet( 0, 0, 1, 0 );
	XMVECTOR camUpBasis = XMVectorSet( 0, 1, 0, 0 );
	XMFLOAT3 camWorldPos = { 0,0,0 };

	gpu_data gpuData = {};
	frame_data frameData = {};

	VkBackendInit();

	// NOTE: time is a double of seconds
	// NOTE: t0 = double( UINT64( 1ULL << 32 ) ) -> precision mostly const for the next ~136 years;
	// NOTE: double gives time precision of 1 uS
	BOOL				isRunning = true;
	const u64			FREQ = SysGetCpuFreq();
	//constexpr double	dt = 0.01;
	//double				t = double( UINT64( 1ULL << 32 ) );
	//double				accumulator = 0;
	u64					currentTicks = SysTicks();

	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = { SCREEN_WIDTH,SCREEN_HEIGHT };
	io.Fonts->AddFontDefault();
	io.Fonts->Build();


	constexpr char pakPath[] = //"Assets/data.pak";
		"Assets/cyberbaron.drak";
	static bool loadedPakFile = false;

	// TODO: QUIT immediately ?
	while( isRunning )
	{
		const u64 newTicks = SysTicks();
		const double elapsedSecs = double( newTicks - currentTicks ) / double( FREQ );
		currentTicks = newTicks;
		//accumulator += elapsedSecs;

		isRunning = SysPumpUserInput( &m, &kbd, 1 );


		// TODO: smooth camera some more ?
		// TODO: wtf Newton ?
		float moveSpeed = camSpeed;// *elapsedSecs;

		XMVECTOR camMove = XMVectorSet( 0, 0, 0, 0 );
		if( kbd.w ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, 1, 0 ) );
		if( kbd.a ) camMove = XMVectorAdd( camMove, XMVectorSet( -1, 0, 0, 0 ) );
		if( kbd.s ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, -1, 0 ) );
		if( kbd.d ) camMove = XMVectorAdd( camMove, XMVectorSet( 1, 0, 0, 0 ) );
		if( kbd.space ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 1, 0, 0 ) );
		if( kbd.c ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, -1, 0, 0 ) );

		XMMATRIX tRotScale = XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) * XMMatrixScaling( moveSpeed, moveSpeed, moveSpeed );
		camMove = XMVector3Transform( XMVector3Normalize( camMove ), tRotScale );

		XMVECTOR xmCamPos = XMLoadFloat3( &camWorldPos );
		XMVECTOR smoothNewCamPos = XMVectorLerp( xmCamPos, XMVectorAdd( xmCamPos, camMove ), 0.18f * elapsedSecs / 0.0166f );

		// TODO: thresholds
		float moveLen = XMVectorGetX( XMVector3Length( smoothNewCamPos ) );
		XMStoreFloat3( &camWorldPos, smoothNewCamPos );

		yaw = XMScalarModAngle( yaw + m.dx * mouseSensitivity * elapsedSecs );
		pitch = std::clamp( pitch + float( m.dy * mouseSensitivity * elapsedSecs ), -almostPiDiv2, almostPiDiv2 );

		// TRANSF CAM VIEW
		XMVECTOR camLookAt = XMVector3Transform( camFwdBasis, XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
		XMMATRIX view = XMMatrixLookAtLH( smoothNewCamPos, XMVectorAdd( smoothNewCamPos, camLookAt ), camUpBasis );

		XMVECTOR viewDet = XMMatrixDeterminant( view );
		XMMATRIX invView = XMMatrixInverse( &viewDet, view );

		XMStoreFloat4x4A( &frameData.proj, proj );
		XMStoreFloat4x4A( &frameData.activeView, view );
		if( !kbd.f )
		{
			XMStoreFloat4x4A( &frameData.mainView, view );
		}
		frameData.worldPos = camWorldPos;
		XMStoreFloat3( &frameData.camViewDir, XMVectorNegate( invView.r[ 2 ] ) );

		// NOTE: inv( A * B ) = inv B * inv A
		XMMATRIX invFrustMat = XMMatrixMultiply( XMLoadFloat4x4A( &frameData.mainView ), proj );
		XMVECTOR det = XMMatrixDeterminant( invFrustMat );
		assert( XMVectorGetX( det ) );
		XMMATRIX frustMat = XMMatrixInverse( &det, invFrustMat );
		XMStoreFloat4x4A( &frameData.frustTransf, frustMat );

		XMStoreFloat4x4A( &frameData.activeProjView, XMMatrixMultiply( XMLoadFloat4x4A( &frameData.activeView ), proj ) );
		XMStoreFloat4x4A( &frameData.mainProjView, XMMatrixMultiply( XMLoadFloat4x4A( &frameData.mainView ), proj ) );

		frameData.elapsedSeconds = elapsedSecs;
		frameData.freezeMainView = kbd.f;
		frameData.dbgDraw = kbd.o;

		ImGui::NewFrame();
		// TODO: make own small efficient string
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

	return 0;
}