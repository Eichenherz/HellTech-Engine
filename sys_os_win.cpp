#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#include <hidusage.h>
#include <shlwapi.h>

#include <assert.h>
#include <stdlib.h>

#include <io.h>
#include <fcntl.h>
#include <iostream>

#include <algorithm>

// TODO: fix build msbuild ? add more stuff ?
// TODO: use own strchyy buffer/ allocator
#include "diy_pch.h"
using namespace std;

#include "sys_os_api.h"
#include "core_types.h"
#include "core_lib_api.h"

#include "r_data_structs.h"

// TODO: msg errors
// TODO: handle debug func better
// TODO: macro for winapi errors 
// TODO: all streams to console  
// TODO: customize console  
static inline void SysBindIOStreamToConsole()
{
#ifdef _DEBUG
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

	assert( dup2Res != -1 );

	setvbuf( stdout, 0, _IONBF, 0 );
	

	// Clear the error state for each of the C++ standard stream objects. We need to do this, as attempts to access the
	// standard streams before they refer to a valid target will cause the iostream objects to enter an error state. In
	// versions of Visual Studio after 2005, this seems to always occur during startup regardless of whether anything
	// has been read from or written to the targets or not.
	std::wcout.clear();
	std::cout.clear();
#endif//_DEBUG
}
static inline void SysOsCreateConsole()
{
#ifdef _DEBUG

	if( !AllocConsole() ){
		OutputDebugString( "Couldn't create console !" );
		quick_exit( EXIT_FAILURE );
	}
	SysBindIOStreamToConsole();

#endif // _DEBUG
}
static inline void SysOsKillConsole()
{
#ifdef _DEBUG
	FreeConsole();
#endif//_DEBUG
}
static inline void SysDbgPrint( const char* str )
{
	OutputDebugString( str );
}
static inline void SysErrMsgBox( const char* str )
{
	UINT behaviour = MB_OK | MB_ICONERROR | MB_APPLMODAL;
	MessageBox( 0, str, 0, behaviour );
}
static inline u32 SysGetFileAbsPath( const char* fileName, char* buffer, u64 buffSize )
{
	static_assert( sizeof( DWORD ) == sizeof( u32 ) );
	return GetFullPathNameA( fileName, buffSize, buffer, 0 );
}

constexpr char	QIY[] = "DiY";
constexpr char	WINDOW_TITLE[] = "Quake id Yourself";

SYSTEM_INFO		sysInfo = {};
HINSTANCE		hInst = 0;
HWND			hWnd = 0;
void*			sysMem = 0;



enum cvar_type : u8
{
	CVAR_INT = 0,
	CVAR_FLOAT = 1,
	CVAR_STRING = 2,
	CVAR_COUNT
};

enum cvar_usage : u8
{

};

struct cvar_parameter
{
	char		name[ 64 ];
	char		description[ 128 ];
	cvar_type	type;
	cvar_usage	usg;
	u32			idx;
};

struct cvar_buffer
{
	static constexpr u32 MAX_FLOAT_COUNT = 100;
	static constexpr u32 MAX_INT_COUNT = 100;
	static constexpr u32 MAX_STRING_COUNT = 20;


};

static b32 frustumCullDbg = 0;

constexpr u16 VK_W = 0x57;
constexpr u16 VK_A = 0x41;
constexpr u16 VK_S = 0x53;
constexpr u16 VK_D = 0x44;
constexpr u16 VK_C = 0x43;
constexpr u16 VK_F = 0x46;
constexpr u16 VK_O = 0x4F;



inline DirectX::XMMATRIX PerspInvDepthInfFovLH( float fovYRads, float aspectRatioWH, float zNear )
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

struct mouse
{
	float dx, dy;
};

struct keyboard
{
	b32 w, a, s, d;
	b32 c;
	b32 space, lctrl;
	b32 f, o;
};

static inline u64		SysDllLoad( const char* name )
{
	return (u64) LoadLibrary( name );
}
static inline void		SysDllUnload( u64 hDll )
{
	if( !hDll ) return;
	if( !FreeLibrary( (HMODULE) hDll ) )
	{
		//handle err
	}
}
static inline void*		SysGetProcAddr( u64 hDll, const char* procName )
{
	return (void*)GetProcAddress( (HMODULE) hDll, procName );
}

static inline double	SysGetCPUPeriod()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	return 1.0 / double( freq.QuadPart );
}
static inline double	SysTicks()
{
	LARGE_INTEGER tick;
	QueryPerformanceCounter( &tick );
	return double( tick.QuadPart );
}
// TODO: remake
static inline b32		SysPumpUserInput( mouse* m, keyboard* kbd, b32 insideWnd )
{
	MSG msg;
	while( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ) ){
		TranslateMessage( &msg );
		DispatchMessageA( &msg );
		if( msg.message == WM_QUIT ) return false;

		m->dx = 0;
		m->dy = 0;

		if( msg.message == WM_INPUT ){
			RAWINPUT ri;
			u32 rawDataSize = sizeof( ri );
			// TODO: how to handle GetRawInputData errors ?
			if( GetRawInputData( (HRAWINPUT) msg.lParam,
								 RID_INPUT,
								 &ri,
								 &rawDataSize,
								 sizeof( RAWINPUTHEADER ) ) == (UINT) -1 ) continue;

			if( ri.header.dwType == RIM_TYPEKEYBOARD ){
				b32 isPressed = !( ri.data.keyboard.Flags & RI_KEY_BREAK );
				b32 isE0 = ( ri.data.keyboard.Flags & RI_KEY_E0 );
				b32 isE1 = ( ri.data.keyboard.Flags & RI_KEY_E1 );

				if( ri.data.keyboard.VKey == VK_W ) kbd->w = isPressed;
				if( ri.data.keyboard.VKey == VK_A ) kbd->a = isPressed;
				if( ri.data.keyboard.VKey == VK_S ) kbd->s = isPressed;
				if( ri.data.keyboard.VKey == VK_D ) kbd->d = isPressed;
				if( ri.data.keyboard.VKey == VK_C ) kbd->c = isPressed;
				if( ri.data.keyboard.VKey == VK_SPACE ) kbd->space = isPressed;
				if( ri.data.keyboard.VKey == VK_CONTROL && !isE0 ) kbd->lctrl = isPressed;
				if( ( ri.data.keyboard.VKey == VK_F ) && isPressed ) kbd->f = ~kbd->f;
				if( ( ri.data.keyboard.VKey == VK_O ) && isPressed ) kbd->o = ~kbd->o;

			} 
			if( ( ri.header.dwType == RIM_TYPEMOUSE ) && insideWnd ){
				m->dx = ri.data.mouse.lLastX;
				m->dy = ri.data.mouse.lLastY;
			}
		}
	}

	return true;
}

static inline u8*		SysReadOnlyMemMapFile( const char* file )
{
	// TODO: for final version we don't share files
	HANDLE hFileToMap =  CreateFileA( file,
									 GENERIC_READ, 
									 FILE_SHARE_READ,0, 
									 OPEN_EXISTING,
									 FILE_ATTRIBUTE_READONLY,0 );

	HANDLE hFileMapping = CreateFileMappingA( hFileToMap, 0,
										PAGE_READONLY, 0, 0, 0 );
	// TODO: name files ?
	void* mmFile = MapViewOfFile( hFileMapping,
								  FILE_MAP_READ, 0, 0, 0 );
	CloseHandle( hFileMapping );
	CloseHandle( hFileToMap );
	return (u8*)mmFile;
}
static inline void		SysCloseMemMapFile( void* mmFile )
{
	UnmapViewOfFile( mmFile );
	//mmFile = 0;
}

LRESULT CALLBACK MainWndProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch( uMsg )	{
	case WM_CLOSE:
	case WM_DESTROY:  PostQuitMessage( 0 ); 
		break;
	}
	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}						 

INT WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, INT )
{
	using namespace DirectX;

	SysOsCreateConsole();
	GetSystemInfo( &sysInfo );

	//sysMem = VirtualAlloc( 0, SYS_MEM_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	//if( !sysMem ) quick_exit( EXIT_FAILURE );
	//if( !MemSysInit( (u8*) sysMem, SYS_MEM_BYTES ) ) quick_exit( EXIT_FAILURE );

	hInst = hInstance;
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof( WNDCLASSEX );
	wc.lpfnWndProc = MainWndProc;
	wc.hInstance = hInst;
	wc.hCursor = LoadCursor( 0, IDC_ARROW );
	wc.lpszClassName = QIY;
	if( !RegisterClassEx( &wc ) ) quick_exit( EXIT_FAILURE );
	

	RECT wr = {};
	wr.left = 350;
	wr.top = 100; 
	wr.right = SCREEN_WIDTH + wr.left;
	wr.bottom = SCREEN_HEIGHT + wr.top;
	DWORD windowStyle = WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU;
	AdjustWindowRect( &wr, windowStyle, 0 );
	hWnd = CreateWindow( wc.lpszClassName, WINDOW_TITLE, windowStyle,
						 wr.left,wr.top, wr.right - wr.left, wr.bottom - wr.top, 0,0,
						 hInst, 0 );
	if( !hWnd ) quick_exit( EXIT_FAILURE );
	ShowWindow( hWnd, SW_SHOWDEFAULT );


	RAWINPUTDEVICE hid[ 2 ];
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
	if( !RegisterRawInputDevices( hid, POPULATION( hid ), sizeof( RAWINPUTDEVICE ) ) )
		quick_exit( EXIT_FAILURE );


	mouse m = {};
	keyboard kbd = {};

	constexpr float almostPiDiv2 = 0.995f * DirectX::XM_PIDIV2;
	float mouseSensitivity = 0.1f;
	float camSpeed = 150.0f;
	float moveThreshold = 0.0001f;

	float zNear = 0.5f;
	float drawDistance = 200.0f;
	XMMATRIX proj = PerspInvDepthInfFovLH( XMConvertToRadians( 70.0f ), float( SCREEN_WIDTH ) / float( SCREEN_HEIGHT ), zNear );
	// NOTE: pitch must be in [-pi/2,pi/2]
	float pitch = 0;
	float yaw = 0;

	// TODO: use store/load ?
	XMVECTOR camFwdBasis = XMVectorSet( 0, 0, 1, 0 );
	XMVECTOR camUpBasis = XMVectorSet( 0, 1, 0, 0 );
	XMFLOAT3 camWorldPos = { 0,0,0 };
	

	VkBackendInit();

	// NOTE: t0 = double( UINT64( 1ULL << 32 ) ) -> precision mostly const for the next ~136 years;
	// NOTE: double gives time precision of 1 uS
	BOOL				isRunning = true;
	const double		PERIOD = SysGetCPUPeriod();
	constexpr double	dt = 0.01;
	double				t = double( UINT64( 1ULL << 32 ) );
	double				currentTime = SysTicks() * PERIOD;
	double				accumulator = 0;

	// TODO: re think this 
	cull_info cullData = {};

	// TODO: QUIT immediately ?
	while( isRunning ){
		const double newTime = SysTicks() * PERIOD;
		const double frameTime = newTime - currentTime;
		currentTime = newTime;
		accumulator += frameTime;

		// TODO: 
		//POINT cursorPos;
		//GetCursorPos( &cursorPos );
		//
		//b32 cursorInside = cursorPos.x >= wr.left && cursorPos.x <= wr.right &&
		//	cursorPos.y >= wr.top && cursorPos.y <= wr.bottom;

		isRunning = SysPumpUserInput( &m, &kbd, 1 );

		// PROC INPUT
		
		// TODO: smooth camera some more ?

		float moveSpeed = camSpeed * dt;

		XMVECTOR camMove = XMVectorSet( 0, 0, 0, 0 );
		if( kbd.w ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, 1, 0 ) );
		if( kbd.a ) camMove = XMVectorAdd( camMove, XMVectorSet( -1, 0, 0, 0 ) );
		if( kbd.s ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, -1, 0 ) );
		if( kbd.d ) camMove = XMVectorAdd( camMove, XMVectorSet( 1, 0, 0, 0 ) );
		if( kbd.space ) camMove = XMVectorAdd( camMove,  XMVectorSet( 0, 1, 0, 0 ) );
		if( kbd.c ) camMove =  XMVectorAdd( camMove,  XMVectorSet( 0, -1, 0, 0 ) );

		camMove = XMVector3Transform( XMVector3Normalize( camMove ),
									  XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) *
									  XMMatrixScaling( moveSpeed, moveSpeed, moveSpeed ) );

		XMVECTOR camPos = XMLoadFloat3( &camWorldPos );
		XMVECTOR smoothNewCamPos = XMVectorLerp( camPos, XMVectorAdd( camPos, camMove ), 0.18f * dt / 0.0166f );

		float moveLen = XMVectorGetX( XMVector3Length( smoothNewCamPos ) );
		//smoothNewCamPos = ( moveLen > moveThreshold ) ? smoothNewCamPos : XMVectorSet( 0, 0, 0, 0 );

		//XMVECTOR oldPose = XMQuaternionRotationRollPitchYaw( pitch, yaw, 0 );
		yaw = XMScalarModAngle( yaw + m.dx * mouseSensitivity * dt );
		pitch = std::clamp( pitch + float( m.dy * mouseSensitivity * dt ), -almostPiDiv2, almostPiDiv2 );

		//XMVECTOR smoothRot = XMQuaternionSlerp( oldPose, XMQuaternionRotationRollPitchYaw( pitch, yaw, 0 ), 0.18f * dt / 0.0166f );

		XMStoreFloat3( &camWorldPos, smoothNewCamPos );
		// TRANSF CAM VIEW
		XMVECTOR camLookAt = XMVector3Transform( camFwdBasis, XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
		XMMATRIX view = XMMatrixLookAtLH( XMLoadFloat3( &camWorldPos ),
										  XMVectorAdd( XMLoadFloat3( &camWorldPos ), camLookAt ),
										  camUpBasis );

		XMVECTOR viewDet = XMMatrixDeterminant( view );
		XMMATRIX invView = XMMatrixInverse( &viewDet, view );
		XMMATRIX transpInvView = XMMatrixTranspose( invView );

		global_data globs = {};
		XMStoreFloat4x4A( &globs.proj, proj );
		XMStoreFloat4x4A( &globs.view, view );
		globs.camPos = camWorldPos;
		XMStoreFloat3( &globs.camViewDir, XMVectorNegate( invView.r[ 2 ] ) );

		// NOTE: transpose for row-major matrices
		XMMATRIX comboMat = XMMatrixTranspose( XMMatrixMultiply( view, proj ) );
		
		XMVECTOR planes[ 4 ];
		planes[ 0 ] = XMVector3Normalize( XMVectorAdd( comboMat.r[ 3 ], comboMat.r[ 0 ] ) );
		planes[ 1 ] = XMVector3Normalize( XMVectorSubtract( comboMat.r[ 3 ], comboMat.r[ 0 ] ) );
		planes[ 2 ] = XMVector3Normalize( XMVectorAdd( comboMat.r[ 3 ], comboMat.r[ 1 ] ) );
		planes[ 3 ] = XMVector3Normalize( XMVectorSubtract( comboMat.r[ 3 ], comboMat.r[ 1 ] ) );
		if( !kbd.f ){
			for( u64 i = 0; i < 4; ++i ) XMStoreFloat4( (XMFLOAT4*) &cullData.planes[ i ], planes[ i ] );
		}

		DirectX::XMVECTOR frustumX = XMVector3Normalize( XMVectorAdd( proj.r[ 3 ], proj.r[ 0 ] ) );
		DirectX::XMVECTOR frustumY = XMVector3Normalize( XMVectorAdd( proj.r[ 3 ], proj.r[ 1 ] ) );
		cullData.frustum[ 0 ] = XMVectorGetX( frustumX );
		cullData.frustum[ 1 ] = XMVectorGetZ( frustumX );
		cullData.frustum[ 2 ] = XMVectorGetY( frustumY );
		cullData.frustum[ 3 ] = XMVectorGetZ( frustumY );
		// TODO: re think this 
		// TODO: get this from matrix directly ?
		//cull_info cullData = {};
		cullData.zNear = zNear;
		cullData.drawDistance = drawDistance;
		cullData.projWidth = XMVectorGetX( proj.r[ 0 ] );
		cullData.projHeight = XMVectorGetY( proj.r[ 1 ] );
		// TODO: get from view matrix ( + world ) ?
		cullData.camPos = camWorldPos;
		// SIMULATION( state, t, dt );
		while( accumulator >= dt ){
			accumulator -= dt;
			t += dt;
		}

		// TODO: re-make passing data to Gfx
		
		// RENDER( state )
		HostFrames( &globs, cullData, kbd.o, dt );
	}


	VkBackendKill();
	VirtualFree( sysMem, 0, MEM_RELEASE );
	SysOsKillConsole();

	return 0;
}

