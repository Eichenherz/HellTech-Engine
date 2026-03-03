#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#pragma comment( lib, "Synchronization.lib" )
#include <windowsx.h>
#include <hidusage.h>
#include <fileapi.h>

#include <iostream>
#include <algorithm>

#include <vector>
#include <string>

#include "sys_os_api.h"
#include "core_types.h"

#include "r_data_structs.h"

#include <System/Win32/win32_err.h>
#include <System/sys_file.h>
#include "zip_pack.h"

#include "ht_math.h"

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
void SysDbgPrint( const char* str )
{
	OutputDebugString( str );
}
void SysErrMsgBox( const char* str )
{
	UINT behaviour = MB_OK | MB_ICONERROR | MB_APPLMODAL;
	MessageBox( 0, str, 0, behaviour );
}

static inline HANDLE WinGetReadOnlyFileHandle( const char* fileName )
{
	DWORD accessMode = GENERIC_READ;
	DWORD shareMode = 0;
	DWORD creationDisp = OPEN_EXISTING;
	DWORD flagsAndAttrs = FILE_ATTRIBUTE_READONLY;
	return CreateFile( fileName, accessMode, shareMode, 0, creationDisp, flagsAndAttrs, 0 );
}
std::vector<u8> SysReadFile( const char* fileName )
{
	HANDLE hfile = WinGetReadOnlyFileHandle( fileName );
	if( hfile == INVALID_HANDLE_VALUE ) return{};

	LARGE_INTEGER fileSize = {};
	WIN_CHECK( GetFileSizeEx( hfile, &fileSize ) );

	std::vector<u8> fileData( fileSize.QuadPart );

	OVERLAPPED asyncIo = {};
	WIN_CHECK( ReadFileEx( hfile, std::data( fileData ), ( DWORD ) fileSize.QuadPart, &asyncIo, 0 ) );
	CloseHandle( hfile );

	return fileData;
}
// TODO: can be any kind of handle
u64 SysGetFileTimestamp( const char* filename )
{
	HANDLE hfile = WinGetReadOnlyFileHandle( filename );
	FILETIME fileTime = {};
	WIN_CHECK( GetFileTime( hfile, 0, 0, &fileTime ) );

	ULARGE_INTEGER timestamp = {};
	timestamp.LowPart = fileTime.dwLowDateTime;
	timestamp.HighPart = fileTime.dwHighDateTime;

	CloseHandle( hfile );

	return u64( timestamp.QuadPart );
}
// TODO: might not want to crash when file can't be written/read
bool SysWriteToFile( const char* filename, const u8* data, u32 sizeInBytes )
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
	HT_ASSERT( XMVectorGetX( det ) != 0 );
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
	WIN_CHECK( FreeLibrary( (HMODULE) hDll ) );
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
	return tick.QuadPart;
}


struct alignas( 4 ) input_state
{
	vec2 posMouse;
	vec2 dMouse;
	bool keyStates[ 512 ];
	bool mouseButtons[ 5 ];
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
			( ( input_state* ) pUserData )->posMouse = { ( float ) GET_X_LPARAM( lParam ), 
				( float ) GET_Y_LPARAM( lParam ) };
		}
		break;
	}
		
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

#include <imgui.h>
#include <ImGuiFileDialog.h>
#include "ht_fixed_string.h"

using imgui_widget_name = fixed_string<16>;

using imgui_window_name = fixed_string<32>;

enum class imgui_widget_type : u32
{
	BUTTON,
	CHECKBOX,
	RADIO,
	TEXT,           // ImGui::Text (read-only label)
	INPUT_TEXT,     // ImGui::InputText
	INPUT_INT,
	INPUT_FLOAT,
	DRAG_INT,
	DRAG_FLOAT,
	DRAG_FLOAT2,
	DRAG_FLOAT3,
	DRAG_FLOAT4,
	SLIDER_INT,
	SLIDER_FLOAT,
	COLOR3,         // ColorEdit3
	COLOR4,         // ColorEdit4
	COMBO,          // dropdown
	LISTBOX,
	TREE_NODE,
	COLLAPSING_HEADER,
	SEPARATOR,
	IMAGE,          // ImGui::Image
	PROGRESS_BAR,
	COUNT
};

using PFN_ImGuiWidgetAction = void( * )( const void* );

void ImGuiLoadFileAction( const void* )
{
	ImGuiFileDialog::Instance()->OpenDialog( "loadFileDlg", "Choose File", ".hpk" );
}

struct imgui_widget
{
	imgui_widget_name name;
	const void* pData;
	PFN_ImGuiWidgetAction Action;
	imgui_widget_type type;
};

struct imgui_window
{
	std::vector<imgui_widget> widgets;
	imgui_window_name name;
	ImGuiWindowFlags flags;
};

void ImGuiHandleWidget( const imgui_widget& widget )
{
	using enum imgui_widget_type;
	switch( widget.type )
	{
	case BUTTON:
	{
		if( ImGui::Button( std::data( widget.name ) ) )
		{
			widget.Action( widget.pData ); 
		}
		
		break;
	}
	case TEXT:    
	{
		ImGui::Text( std::data( widget.name ) );         
		break;
	}
	default: break;
	}
}

using sys_physical_path = fixed_string<MAX_PATH>;

struct ht_load_hpk_req
{
	sys_physical_path path;
};

inline void ImGuiRenderUI( const std::vector<imgui_window>& imguiWnds, std::vector<ht_load_hpk_req>& loadHpkReqs )
{
	static bool initialPosSet = false;

	ImGui::NewFrame();

	for( const imgui_window& imguiWnd : imguiWnds )
	{
		if( !initialPosSet )
		{
			ImGui::SetNextWindowPos( { std::size( imguiWnd.name ) * ImGui::GetFontSize(), ImGui::GetFontSize() },
				ImGuiCond_FirstUseEver );
			ImGui::SetNextWindowSize( {}, ImGuiCond_FirstUseEver );
			initialPosSet = true;
		}
	
		ImGui::Begin( std::data( imguiWnd.name ), 0, imguiWnd.flags );
		for( const imgui_widget& widget : imguiWnd.widgets )
		{
			ImGuiHandleWidget( widget );
		}
		ImGui::End();
	}

	if( ImGuiFileDialog::Instance()->Display( "loadFileDlg" ) )
	{
		if( ImGuiFileDialog::Instance()->IsOk() )
		{
			std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
			loadHpkReqs.push_back( { path.c_str() } );
		}
		ImGuiFileDialog::Instance()->Close();
	}

	ImGui::Render();

	ImGui::EndFrame();
}

inline void SysNameThread( HANDLE hThread, const wchar_t* name )
{
	HT_ASSERT( SUCCEEDED( SetThreadDescription( hThread, name ) ) );
}

constexpr u64 CACHE_LINE_SZ = 64;

#define CACHE_ALIGN alignas( CACHE_LINE_SZ )

// NOTE: bc Win32 expects LONG64 
using atomic64 = i64;

enum sys_thread_signal : i64
{
	SYS_THREAD_SIGNAL_SLEEP = 0,
	SYS_THREAD_SIGNAL_WAKEUP = 1,
	SYS_THREAD_SIGNAL_EXIT = -1
};

#include "System/sys_mem_arena.h"
#include "ht_mtx_queue.h"

// NOTE: for now, we're lazy and will use std::function

using io_job = std::function<void()>;

struct sys_thread_data
{
	CACHE_ALIGN volatile atomic64 signal;
	CACHE_ALIGN virtual_arena     arena;
	mtx_queue<io_job>             jobs;

};

struct sys_thread
{
	HANDLE			  hndl;
	sys_thread_data*  pData;
	DWORD			  threadId;
};

DWORD WINAPI Win32ThreadLoop( LPVOID lpParam )
{
	sys_thread_data& threadCtx = *( sys_thread_data* ) lpParam;

	constexpr sys_thread_signal undesiredVal = SYS_THREAD_SIGNAL_SLEEP;
	static_assert( sizeof( threadCtx.signal ) == sizeof( undesiredVal ) );

	for( ;; )
	{
		for( ;; )
		{
			WaitOnAddress( &threadCtx.signal, ( void* ) &undesiredVal, sizeof( threadCtx.signal ), INFINITE );
			atomic64 capturedVal = InterlockedAddAcquire64( &threadCtx.signal, 0 );

			[[unlikely]] if( SYS_THREAD_SIGNAL_EXIT == capturedVal ) goto EXIT;
			if( undesiredVal != capturedVal ) break;
		}
		
		// NOTE: win doesn't have exchRelase64
		InterlockedExchange64( &threadCtx.signal, SYS_THREAD_SIGNAL_SLEEP );

		for( io_job Job = {}; threadCtx.jobs.TryPop( Job ); )
		{
			std::cout << "Dikin'Baus\n";
		}
	}
	
EXIT:
	return 0;
}

#include "ht_stable_stretchy_buffer.h"

sys_thread SysCreateThread( 
	u64 stackSize, 
	u64 maxScratchPadSize, 
	const wchar_t* name, 
	stable_stretchy_buffer<sys_thread_data>& threadDataBuff 
) {
	sys_thread_data* pData = &threadDataBuff.push_back( {
		.signal = SYS_THREAD_SIGNAL_SLEEP,
		.arena = virtual_arena{ maxScratchPadSize },
		.jobs = { 128 }
	} );

	DWORD threadId;
	HANDLE hThread = CreateThread( NULL, stackSize, Win32ThreadLoop, ( LPVOID ) pData, 0, &threadId );
	HT_ASSERT( INVALID_HANDLE_VALUE != hThread );

	SysNameThread( hThread, name );

	return {
		.hndl = hThread,
		.pData = pData,
		.threadId = threadId
	};
}

INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR, INT )
{
	using namespace DirectX;

	SysOsCreateConsole();

	SysNameThread( GetCurrentThread(), L"Main/Renderer Thread" );

	WIN_CHECK( DirectX::XMVerifyCPUSupport() );

	SYSTEM_INFO sysInfo = {};
	GetSystemInfo( &sysInfo );
	

	stable_stretchy_buffer<sys_thread_data> threadDataBuff = { 2 * MB };

	std::vector<sys_thread> threads;
	threads.push_back( SysCreateThread( 1 * MB, 1 * GB, L"IO Thread", threadDataBuff ) );


	WNDCLASSEX wc = {
		.cbSize = sizeof( WNDCLASSEX ),
		.lpfnWndProc = MainWndProc,
		.hInstance = hInst,
		.hCursor = LoadCursor( 0, IDC_ARROW ),
		.lpszClassName = ENGINE_NAME
	};
	WIN_CHECK( RegisterClassExA( &wc ) );
	
	RECT wr = {
		.left = 350,
		.top = 100,
		.right = ( LONG ) SCREEN_WIDTH + wr.left,
		.bottom = ( LONG ) SCREEN_HEIGHT + wr.top
	};

	input_state inputState = {};

	constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRect( &wr, windowStyle, 0 );
	HWND hWnd = CreateWindow( wc.lpszClassName, WINDOW_TITLE, windowStyle, wr.left,wr.top, 
		wr.right - wr.left, wr.bottom - wr.top, 0,0, hInst, &inputState );
	WIN_CHECK( INVALID_HANDLE_VALUE != hWnd );

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
	WIN_CHECK( RegisterRawInputDevices( hid, std::size( hid ), sizeof( RAWINPUTDEVICE ) ) );

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

	ImGuiContext* imGuiCtx = ImGui::CreateContext();
	ImGui::SetCurrentContext( imGuiCtx );
	ImGui::StyleColorsDark();
	ImGuiIO& imGuiIO = ImGui::GetIO();
	imGuiIO.DisplaySize = { SCREEN_WIDTH,SCREEN_HEIGHT };
	imGuiIO.Fonts->AddFontDefault();
	imGuiIO.Fonts->Build();
	

	std::vector<imgui_window> imguiWnds;
	imguiWnds.push_back( {
		.widgets = { 
			imgui_widget {
				.name = "GPU ms: ",
				.type = imgui_widget_type::TEXT
			} 
		},
		.name = "Renderer Stats",
		.flags = ImGuiWindowFlags_NoScrollbar
	} );

	imguiWnds.push_back( {
		.widgets = { 
			imgui_widget {
			.name = "Load HPK",
			.Action = ImGuiLoadFileAction,
			.type = imgui_widget_type::BUTTON
		} 
		},
		.name = "##bnt_load_hpk",
		.flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | 
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse
	} );

	std::vector<ht_load_hpk_req> loadHpkReqs;

	auto pRenderer = MakeRenderer();

	pRenderer->InitBackend( ( uintptr_t ) hInst, ( uintptr_t ) hWnd );

	// NOTE: time is a double of seconds
	// NOTE: t0 = double( UINT64( 1ULL << 32 ) ) -> precision mostly const for the next ~136 years;
	// NOTE: double gives time precision of 1 uS
	bool				isRunning = true;
	const double		cpuPeriod = 1.0 / double( SysGetCpuFreq() );
	//constexpr double	dt = 0.01;
	//double				t = double( UINT64( 1ULL << 32 ) );
	//double				accumulator = 0;
	u64					currentTicks = SysTicks();

	// TODO: vfs
	constexpr char assetFile[] = "D:/3d models/Nightclub Futuristic/nightclub_futuristic_pub_ambience_asset.hpk";
	unique_mmap_file mmappedFile = SysCreateFile( assetFile, file_permissions_bits::READ,
		file_create_flags::OPEN_IF_EXISTS, file_access_flags::RANDOM );
	vfs_zip_mem vfsFileSys = { *mmappedFile };

	bool vfsMounted = false;

	// TODO: QUIT immediately ?
	while( isRunning )
	{
		const u64 newTicks = SysTicks();
		const double elapsedSecs = double( newTicks - currentTicks ) * cpuPeriod;
		currentTicks = newTicks;
		//accumulator += elapsedSecs;

		inputState.dMouse = {};

		isRunning = SysPumpUserInput();


		if( !vfsMounted )
		{
			sys_thread& ioThread = threads[ 0 ];
			ioThread.pData->jobs.TryPush( [] () {} );

			InterlockedExchange64( &ioThread.pData->signal, SYS_THREAD_SIGNAL_WAKEUP );

			WakeByAddressSingle( ( void* ) &ioThread.pData->signal );

			vfsMounted = true;
		}


		auto[ camMove, dRot ] = GetMoveCamAction( inputState );

		mainActiveCam.Move( camMove, dRot, elapsedSecs );
		debugCam.Move( camMove, dRot, elapsedSecs );

		if( !inputState.keyStates[ VK_F ] )
		{
			view_data mainViewData = mainActiveCam.GetViewData();
			frameData.views[ mainViewIdx ] = mainViewData;

			mainActiveCam.prevViewProj = mainViewData.mainViewProj;
		}
		
		frameData.views[ dbgViewIdx ] = debugCam.GetViewData();

		XMMATRIX frustMat = FrustumMatrixFromViewProj( XMLoadFloat4x4A( &frameData.views[ mainViewIdx ].mainViewProj ) );
		XMStoreFloat4x4A( &frameData.frustTransf, frustMat );

		frameData.elapsedSeconds = elapsedSecs;
		frameData.freezeMainView = inputState.keyStates[ VK_F ];
		frameData.dbgDraw = inputState.keyStates[ VK_O ];

		imGuiIO.DeltaTime = elapsedSecs;
		imGuiIO.MousePos = { inputState.posMouse.x, inputState.posMouse.y };
		imGuiIO.MouseDown[ 0 ] = inputState.mouseButtons[ 0 ];
		imGuiIO.MouseDown[ 1 ] = inputState.mouseButtons[ 1 ];
		imGuiIO.MouseDown[ 2 ] = inputState.mouseButtons[ 2 ];

		ImGuiRenderUI( imguiWnds, loadHpkReqs );

		pRenderer->HostFrames( frameData, gpuData );
	}

	return 0;
}

