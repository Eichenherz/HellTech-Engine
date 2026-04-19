#include "Win32/DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#pragma comment( lib, "Synchronization.lib" )
#include <windowsx.h>
#include <hidusage.h>
#include <fileapi.h>

#include <iostream>
#include <algorithm>

#include <ranges>
#include <vector>
#include <string>

#include "sys_os_api.h"
#include "ht_core_types.h"

#include <Win32/win32_err.h>
#include <System/sys_file.h>
#include <System/sys_sync.h>
#include <System/sys_thread.h>

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

// TODO: might not want to crash when file can't be written/read
bool SysWriteToFile( const char* filename, const u8* data, u32 sizeInBytes )
{
	DWORD accessMode	= GENERIC_WRITE;
	DWORD shareMode		= FILE_SHARE_READ | FILE_SHARE_WRITE;
	DWORD creationDisp	= OPEN_ALWAYS;
	DWORD flagsAndAttrs = FILE_ATTRIBUTE_NORMAL;
	HANDLE hFile		= CreateFile( filename, accessMode, shareMode, 0,
		creationDisp, flagsAndAttrs, 0 );

	OVERLAPPED asyncIo = {};
	WIN_CHECK( !WriteFileEx( hFile, data, sizeInBytes, &asyncIo, 0 ) );
	CloseHandle( hFile );

	return true;
}

constexpr char	ENGINE_NAME[] = "helltech_engine";
constexpr char	WINDOW_TITLE[] = "HellTech Engine";

using PFN_XMLookAtCoord = DirectX::XMMATRIX ( XM_CALLCONV * ) (
	DirectX::FXMVECTOR eyePos,
	DirectX::FXMVECTOR focusPos,
	DirectX::FXMVECTOR upDir
);

struct virtual_camera
{
	float4x4			proj = {};
	float4x4			prevView = {};
	float4x4			prevViewProj = {};
	float3 				fwdBasis = {};
	float3 				upBasis = {};
	float3				worldPos = { 0.0f, 0.0f, 0.0f };

	PFN_XMLookAtCoord	LookAt = nullptr;

	// NOTE: pitch must be in [-pi/2,pi/2]
	float				pitch = 0.0f;
	float				yaw = 0.0f;

	float				speed = 1.5f;

	inline void XM_CALLCONV Move( DirectX::XMVECTOR camMove, float2 dRot, float elapsedSecs )
	{
		using namespace DirectX;

		yaw = XMScalarModAngle( yaw + dRot.x * elapsedSecs );
		pitch = std::clamp( pitch + dRot.y * elapsedSecs, -HT_ALMOST_HALF_PI, HT_ALMOST_HALF_PI );

		XMMATRIX tRotScale = XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) * XMMatrixScaling( speed, speed, speed );
		XMVECTOR xmCamMove = XMVector3Transform( XMVector3Normalize( camMove ), tRotScale );

		XMVECTOR xmCamPos = XMLoadFloat3( &worldPos );
		float lerpTimeFactor = 0.18f * elapsedSecs / 0.0166f;
		XMVECTOR smoothNewCamPos = XMVectorLerp( xmCamPos, XMVectorAdd( xmCamPos, xmCamMove ), lerpTimeFactor );

		// TODO: thresholds
		//float moveLen = XMVectorGetX( XMVector3Length( smoothNewCamPos ) );
		worldPos = DX_XMStoreFloat3( smoothNewCamPos );
	}

	inline view_data GetViewData() const
	{
		using namespace DirectX;

		XMVECTOR xmFwd = XMLoadFloat3( &fwdBasis );
		XMVECTOR xmUp = XMLoadFloat3( &upBasis );
		XMVECTOR xmWorldPos = XMLoadFloat3( &worldPos );

		XMVECTOR camLookAt = XMVector3Transform( xmFwd, XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
		XMMATRIX view = LookAt( xmWorldPos, XMVectorAdd( xmWorldPos, camLookAt ), xmUp );

		XMVECTOR viewDet = XMMatrixDeterminant( view );
		XMMATRIX invView = XMMatrixInverse( &viewDet, view );
		XMVECTOR viewDir = XMVectorNegate( invView.r[ 2 ] );

		XMMATRIX xmProj = XMLoadFloat4x4A( &proj );

		return {
			.proj			= proj,
			.mainView		= DX_XMStoreFloat4x4( view ),
			//.prevView		= DX_XMStoreFloat4x4(),
			.mainViewProj	= DX_XMStoreFloat4x4( XMMatrixMultiply( view, xmProj ) ),
			.prevViewProj	= prevViewProj,
			.worldPos		= worldPos,
			// NOTE: this must not be negative for LH coords
			.camViewDir		= DX_XMStoreFloat3( viewDir )
		};
	}
};

inline virtual_camera MakeVirtualCameraWithProjLH( float radsYFov, float aspectRatioWH, float zNear )
{
	return {
		.proj = ProjWithRevZInfFarFromFovAndAspectRatio( radsYFov, aspectRatioWH, zNear, false ),
		.fwdBasis = { 0.0f, 0.0f, 1.0f },
		.upBasis = { 0.0f, 1.0f, 0.0f },
		.LookAt = DirectX::XMMatrixLookAtLH
	};
}

inline virtual_camera MakeVirtualCameraWithProjRH( float radsYFov, float aspectRatioWH, float zNear )
{
	return {
		.proj = ProjWithRevZInfFarFromFovAndAspectRatio( radsYFov, aspectRatioWH, zNear, true ),
		.fwdBasis = { 0.0f, 0.0f, -1.0f },
		.upBasis = { 0.0f, 1.0f, 0.0f },
		.LookAt = DirectX::XMMatrixLookAtRH
	};
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


struct input_state
{
	float 	mouseX;
	float 	mouseY;
	i32 	mouseDx;
	i32 	mouseDy;
	bool	keyStates[ 512 ];
	bool 	mouseButtons[ 5 ];
};

#include <System/Win32/win32_kbd_scancodes.h>

struct move_cam_action
{
	DirectX::XMVECTOR	camMove;
	float2				dRot;
};

inline move_cam_action GetMoveCamAction( const input_state& inputState, float mouseSensitivity )
{
	using namespace DirectX;

	XMVECTOR camMove = XMVectorSet( 0, 0, 0, 0 );
	if( inputState.keyStates[ HT_SC_W ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, 1, 0 ) );
	if( inputState.keyStates[ HT_SC_A ] ) camMove = XMVectorAdd( camMove, XMVectorSet( -1, 0, 0, 0 ) );
	if( inputState.keyStates[ HT_SC_S ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, -1, 0 ) );
	if( inputState.keyStates[ HT_SC_D ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 1, 0, 0, 0 ) );
	if( inputState.keyStates[ HT_SC_SPACE ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 1, 0, 0 ) );
	if( inputState.keyStates[ HT_SC_C ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, -1, 0, 0 ) );

	if( !XMVector3Equal( camMove, XMVectorZero() ) )
	{
		camMove = XMVector3Normalize( camMove );
	}

	float2 yawPitch = {
		( float ) inputState.mouseDx * mouseSensitivity,
		( float ) inputState.mouseDx * mouseSensitivity
	};
	return { .camMove = camMove, .dRot = yawPitch };
}

static inline bool SysPumpUserInput()
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

#include <imgui.h>
#include <ImGuiFileDialog.h>
#include "ht_fixed_string.h"

struct im_gui_ctx
{
	ImGuiContext*	ctx;
	ImGuiIO*		io;

	im_gui_ctx( u32 width, u32 height )
	{
		ctx = ImGui::CreateContext();
		ImGui::SetCurrentContext( ctx );
		ImGui::StyleColorsDark();
		io = &ImGui::GetIO();
		io->DisplaySize = { ( float ) width, ( float ) height };
		io->Fonts->AddFontDefault();
		io->Fonts->Build();
	}

	inline void UpdateTimeAndInputState( float elapsedSecs, const input_state& inputState )
	{
		io->DeltaTime = elapsedSecs;
		io->MousePos = { inputState.mouseX, inputState.mouseY };
		io->MouseDown[ 0 ] = inputState.mouseButtons[ 0 ];
		io->MouseDown[ 1 ] = inputState.mouseButtons[ 1 ];
		io->MouseDown[ 2 ] = inputState.mouseButtons[ 2 ];
	}
};

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
	imgui_widget_name		name;
	const void*				pData;
	PFN_ImGuiWidgetAction	Action;
	imgui_widget_type		type;
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



constexpr u64 CACHE_LINE_SZ = std::hardware_destructive_interference_size;

#define CACHE_ALIGN alignas( CACHE_LINE_SZ )



#include "ht_mem_arena.h"
#include "ht_mtx_queue.h"


struct renderer_upload_job
{
	std::vector<mesh_upload_req>	reqs;
	renderer_interface*				pRI;

	inline void operator()( virtual_arena& arena )
	{
		return pRI->UploadMeshes( reqs, arena );
	}
};

struct io_job
{
	enum type_t : u8
	{
		UPLOAD
	};

	union
	{
		renderer_upload_job upload;
	};
	type_t type;

	io_job() : upload{}, type{ UPLOAD } {}

	io_job( const renderer_upload_job& up ) : upload{ up }, type{ UPLOAD } {}

	io_job( renderer_upload_job&& up ) : upload{ MOV( up ) }, type{ UPLOAD } {}

	~io_job() { upload.~renderer_upload_job(); }

	io_job( const io_job& o ) : upload{ o.upload }, type{ o.type } {}
	io_job( io_job&& o )      : upload{ MOV( o.upload ) }, type{ o.type } {}

	io_job& operator=( const io_job& o ) { type = o.type; upload = o.upload; return *this; }
	io_job& operator=( io_job&& o )      { type = o.type; upload = MOV( o.upload ); return *this; }
};


struct sys_thread_data
{
	CACHE_ALIGN ht_atomic64		signal;
	CACHE_ALIGN virtual_arena	arena;
	mtx_queue<io_job>           jobs;
};

struct sys_thread
{
	HANDLE			  			hndl;
	sys_thread_data*  			pData;
	DWORD			  			threadId;
};

DWORD WINAPI Win32ThreadLoop( LPVOID lpParam )
{
	sys_thread_data& threadCtx = *( sys_thread_data* ) lpParam;

	constexpr sys_thread_signal UNDESIRED_VAl = SYS_THREAD_SIGNAL_SLEEP;
	static_assert( sizeof( threadCtx.signal ) == sizeof( UNDESIRED_VAl ) );

	for( ;; )
	{
		for( ;; )
		{
			sys_thread_signal capturedVal = ( sys_thread_signal ) SysAtomicWaitOnAddr(
				threadCtx.signal, ( void* ) &UNDESIRED_VAl, INFINITE );

			[[unlikely]] if( SYS_THREAD_SIGNAL_EXIT == capturedVal ) goto EXIT;
			if( UNDESIRED_VAl != capturedVal ) break;
		}

		for( io_job jobData = {}; threadCtx.jobs.TryPop( jobData ); )
		{
			jobData.upload( threadCtx.arena );
		}

		// NOTE: win doesn't have exchRelase64
		InterlockedExchange64( &threadCtx.signal, SYS_THREAD_SIGNAL_SLEEP );
	}
	
EXIT:
	return 0;
}

#include "ht_stretchybuff.h"


sys_thread SysCreateThread( 
	u64									stackSize,
	u64									maxScratchPadSize,
	const wchar_t*						name,
	ht_stretchybuff<sys_thread_data>&	threadDataBuff
) {
	sys_thread_data* pData = &threadDataBuff.push_back( {
		.signal = SYS_THREAD_SIGNAL_SLEEP,
		.arena	= virtual_arena{ maxScratchPadSize },
		.jobs	= { 128 }
	} );

	DWORD threadId;
	HANDLE hThread = CreateThread( NULL, stackSize, Win32ThreadLoop, ( LPVOID ) pData, 0, &threadId );
	HT_ASSERT( INVALID_HANDLE_VALUE != hThread );

	SysNameThread( ( u64 ) hThread, name );

	return {
		.hndl		= hThread,
		.pData		= pData,
		.threadId	= threadId
	};
}

#include "engine_types.h"

INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR, INT )
{
	using namespace DirectX;

	SysOsCreateConsole();

	SysNameThread( ( u64 ) GetCurrentThread(), L"Main/Renderer Thread" );
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
	
	RECT wr = {
		.left	= 350,
		.top	= 100,
		.right	= ( LONG ) SCREEN_WIDTH + wr.left,
		.bottom = ( LONG ) SCREEN_HEIGHT + wr.top
	};

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


	virtual_arena persistentArena = { 10 * GB };
	virtual_arena scratchArena = { 10 * MB };

	ht_stretchybuff<sys_thread_data> threadDataBuff = HtANewStretchyBuffFromArena<sys_thread_data>(
		persistentArena, 8 /* cores */ );

	std::vector<sys_thread> threads;
	threads.push_back( SysCreateThread( 1 * MB, 1 * GB, L"IO Thread", threadDataBuff ) );

	sys_thread& ioThread = threads[ 0 ];

	constexpr float fovRads = XMConvertToRadians( 70.0f );
	constexpr float aspecRatioWH = float( SCREEN_WIDTH ) / float( SCREEN_HEIGHT );
	constexpr float zNear = 0.5f;

	virtual_camera mainActiveCam = MakeVirtualCameraWithProjLH( fovRads, aspecRatioWH, zNear );
	virtual_camera debugCam = MakeVirtualCameraWithProjLH( fovRads, aspecRatioWH, zNear );

	{
		view_data mainViewData = mainActiveCam.GetViewData();
		mainActiveCam.prevViewProj = mainViewData.mainViewProj;
	}

	gpu_data	gpuData = {};

	im_gui_ctx imGuiCtx = { SCREEN_WIDTH, SCREEN_HEIGHT };


	std::vector<imgui_window> imguiWnds;
	imguiWnds.push_back( {
		.widgets = { 
			imgui_widget {
				.name = "GPU ms: ",
				.type = imgui_widget_type::TEXT
			} 
		},
		.name	= "Renderer Stats",
		.flags	= ImGuiWindowFlags_NoScrollbar
	} );

	//imguiWnds.push_back( {
	//	.widgets = {
	//		imgui_widget {
	//			.name	= "Load HPK",
	//			.Action = ImGuiLoadFileAction,
	//			.type	= imgui_widget_type::BUTTON
	//	    }
	//	},
	//	.name	= "##bnt_load_hpk",
	//	.flags	= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
	//	ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse
	//} );

	std::vector<ht_load_hpk_req> loadHpkReqs;


	std::unique_ptr<renderer_interface> pRenderer = MakeRenderer();

	pRenderer->InitBackend( ( uintptr_t ) hInst, ( uintptr_t ) hWnd );

	constexpr float almostPiDiv2 = 0.995f * DirectX::XM_PIDIV2;
	float			mouseSensitivity = 0.1f;
	float			camSpeed = 1.5f;
	float			moveThreshold = 0.0001f;
	// NOTE: time is a double of seconds
	// NOTE: t0 = double( UINT64( 1ULL << 32 ) ) -> precision mostly const for the next ~136 years;
	// NOTE: double gives time precision of 1 uS
	bool			isRunning = true;
	const double	cpuPeriod = 1.0 / double( SysGetCpuFreq() );
	//constexpr double	dt = 0.01;
	//double				t = double( UINT64( 1ULL << 32 ) );
	//double				accumulator = 0;
	u64				currentTicks = SysTicks();

	// TODO: vfs
	constexpr char	assetFile[] = "D:/3d models/Nightclub Futuristic/nightclub_futuristic_pub_ambience_asset.hpk";
	mmap_file		mmappedFile = SysCreateMmapFile( assetFile, file_permissions_bits::READ,
		file_create_flags::OPEN_IF_EXISTS, file_access_flags::RANDOM );
	vfs_zip_mem		vfs = { mmappedFile };

	ht_stretchybuff<gpu_instance> flatSceneGraph;

	bool vfsMounted = false;

	while( isRunning )
	{
		stack_adaptor virtualStack = { scratchArena };

		const u64 newTicks = SysTicks();
		const double elapsedSecs = double( newTicks - currentTicks ) * cpuPeriod;
		currentTicks = newTicks;
		//accumulator += elapsedSecs;

		inputState.mouseDx = 0.0f;
		inputState.mouseDy = 0.0f;

		isRunning = SysPumpUserInput();


		if( !vfsMounted )
		{
			auto meshFiles = vfs.files | std::views::keys | std::views::filter( 
				[] ( const vfs_path& vpath ) { return ( nullptr != std::strstr( std::data( vpath ), ".mesh" ) ); } );

			//auto texFiles = vfs.files | std::views::keys | std::views::filter(
			//	[] ( const vfs_path& vpath ) { return ( nullptr != std::strstr( std::data( vpath ), ".dds" ) ); } );

			auto levelFiles = vfs.files | std::views::keys | std::views::filter( 
				[] ( const vfs_path& vpath ) { return ( nullptr != std::strstr( std::data( vpath ), ".lvl" ) ); } );

			ankerl::unordered_dense::map<u64, HRNDMESH32> meshIdMap;
			std::vector<mesh_upload_req> uploads;
			for( const vfs_path& vpath : meshFiles )
			{
				u64 pathHash = std::hash<vfs_path>{}( vpath );
				// TODO: might wanna check on content hash too
				if( std::cend( meshIdMap ) != meshIdMap.find( pathHash ) ) continue;

				std::span<const u8> rawBytes = vfs.GetFileByteView( vpath );
				HT_ASSERT( std::size( rawBytes ) );
				hellpack_mesh_asset mesh = HpkReadBinaryBlob<hellpack_mesh_asset>( rawBytes );

				HRNDMESH32 hMesh =  pRenderer->AllocMeshComponent();

				uploads.push_back( { .filepath = vpath, .htAsset = mesh, .hSlot = hMesh } );

				meshIdMap.emplace( pathHash, hMesh );
			}

			ioThread.pData->jobs.TryPush( renderer_upload_job{ .reqs = std::move( uploads ), .pRI = pRenderer.get() } );

			SysAtomicSignalSingleThread( ioThread.pData->signal, SYS_THREAD_SIGNAL_WAKEUP );

			//ankerl::unordered_dense::map<u64, u32> texIdMap;
			//for( const vfs_path& vpath : texFiles )
			//{
			//	u64 pathHash = std::hash<vfs_path>{}( vpath );
			//	// TODO: might wanna check on content hash too
			//	if( std::cend( texIdMap ) != texIdMap.find( pathHash ) ) continue;
			//}

			// TODO: must reserve before alloc
			HT_ASSERT( 1 == std::ranges::distance( levelFiles ) );
			for( const vfs_path& vpath : levelFiles )
			{
				std::span<const u8> rawBytes = vfs.GetFileByteView( vpath );
				hellpack_level lvl = HpkReadBinaryBlob<hellpack_level>( rawBytes );

				flatSceneGraph = HtANewStretchyBuffFromArena<gpu_instance>(
					persistentArena, std::size( lvl.nodes ) );
				for( const world_node& node : lvl.nodes )
				{
					auto it = meshIdMap.find( node.meshHash );
					if( std::cend( meshIdMap ) == it ) continue;
					flatSceneGraph.push_back( { .transform = node.toWorld, .meshIdx = it->second } );
				}
			}
			
			vfsMounted = true;
		}


		auto[ camMove, dRot ] = GetMoveCamAction( inputState, mouseSensitivity );

		mainActiveCam.Move( camMove, dRot, elapsedSecs );
		debugCam.Move( camMove, dRot, elapsedSecs );

		imGuiCtx.UpdateTimeAndInputState( elapsedSecs, inputState );

		std::pmr::vector<view_data> views{ &virtualStack };

		view_data mainViewData = mainActiveCam.GetViewData();

		if( !inputState.keyStates[ HT_SC_F ] )
		{
			views.push_back( mainViewData );
			mainActiveCam.prevViewProj = mainViewData.mainViewProj;
		}
		
		views.push_back( debugCam.GetViewData() );

		XMMATRIX frustMat = FrustumMatrixFromViewProj( XMLoadFloat4x4A( &mainViewData.mainViewProj ) );
		
		frame_data frameData = {
			.views 			= views,
			.instances 		= flatSceneGraph,
			.frustTransf	= DX_XMStoreFloat4x4( frustMat ),
			.elapsedSeconds = ( float ) elapsedSecs,
			.freezeMainView = inputState.keyStates[ HT_SC_F ],
			.dbgDraw		= inputState.keyStates[ HT_SC_O ]
		};

		ImGuiRenderUI( imguiWnds, loadHpkReqs );

		pRenderer->HostFrames( frameData, gpuData );
	}

	return 0;
}

