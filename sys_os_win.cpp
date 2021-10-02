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

#include <algorithm>

// TODO: fix build msbuild ? add more stuff ?
// TODO: use own strchyy buffer/ allocator
#include "diy_pch.h"
using namespace std;

#include "sys_os_api.h"
#include "core_types.h"
#include "core_lib_api.h"

#include "r_data_structs.h"

inline void Win32WriteLastErr( LPTSTR lpsLineFile )
{
	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				   0, dw,
				   MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
				   (LPTSTR) &lpMsgBuf, 0, 0 );

	lpDisplayBuf = 
		(LPVOID) LocalAlloc( LMEM_ZEROINIT, ( lstrlen( (LPCTSTR) lpMsgBuf ) + lstrlen( (LPCTSTR) lpsLineFile ) + 40 ) );
	StringCchPrintf( (LPTSTR) lpDisplayBuf,
					 LocalSize( lpDisplayBuf ),
					 TEXT( "%s code %d: %s" ),
					 lpsLineFile, dw, lpMsgBuf );
	MessageBox( 0, (LPCTSTR) lpDisplayBuf, TEXT( "Error" ), MB_OK | MB_ICONERROR | MB_APPLMODAL );

	LocalFree( lpMsgBuf );
	LocalFree( lpDisplayBuf );
}

#define WIN_CHECK( win )													\
do{																			\
	constexpr char WIN_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR"\nERR: ";		\
	if( win )																\
	{																		\
		Win32WriteLastErr( ( LPSTR ) WIN_ERR_STR );							\
		abort();															\
	}																		\
}while( 0 )	

// TODO: handle debug func better
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

	WIN_CHECK( !AllocConsole() );
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

static bool frustumCullDbg = 0;

constexpr u16 VK_W = 0x57;
constexpr u16 VK_A = 0x41;
constexpr u16 VK_S = 0x53;
constexpr u16 VK_D = 0x44;
constexpr u16 VK_C = 0x43;
constexpr u16 VK_F = 0x46;
constexpr u16 VK_O = 0x4F;


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
// TODO: remake
static inline bool	SysPumpUserInput( mouse* m, keyboard* kbd, bool insideWnd )
{
	MSG msg;
	while( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ) )
	{
		TranslateMessage( &msg );
		DispatchMessageA( &msg );
		if( msg.message == WM_QUIT ) return false;

		m->dx = 0;
		m->dy = 0;

		if( msg.message == WM_INPUT )
		{
			RAWINPUT ri = {};
			u32 rawDataSize = sizeof( ri );
			// TODO: how to handle GetRawInputData errors ?
			UINT res = GetRawInputData( ( HRAWINPUT )msg.lParam, RID_INPUT, &ri, &rawDataSize, sizeof( RAWINPUTHEADER ) );
			if( res == UINT( -1 ) ) continue;

			if( ri.header.dwType == RIM_TYPEKEYBOARD )
			{
				bool isPressed = !( ri.data.keyboard.Flags & RI_KEY_BREAK );
				bool isE0 = ( ri.data.keyboard.Flags & RI_KEY_E0 );
				bool isE1 = ( ri.data.keyboard.Flags & RI_KEY_E1 );

				if( ri.data.keyboard.VKey == VK_W ) kbd->w = isPressed;
				if( ri.data.keyboard.VKey == VK_A ) kbd->a = isPressed;
				if( ri.data.keyboard.VKey == VK_S ) kbd->s = isPressed;
				if( ri.data.keyboard.VKey == VK_D ) kbd->d = isPressed;
				if( ri.data.keyboard.VKey == VK_C ) kbd->c = isPressed;
				if( ri.data.keyboard.VKey == VK_SPACE ) kbd->space = isPressed;
				if( ri.data.keyboard.VKey == VK_CONTROL && !isE0 ) kbd->lctrl = isPressed;
				if( ( ri.data.keyboard.VKey == VK_F ) && isPressed ) kbd->f = !kbd->f;
				if( ( ri.data.keyboard.VKey == VK_O ) && isPressed ) kbd->o = !kbd->o;
				if( ( ri.data.keyboard.VKey == VK_ESCAPE ) && isPressed ) kbd->esc = !kbd->esc;

			}
			if( ( ri.header.dwType == RIM_TYPEMOUSE ) && insideWnd )
			{
				m->dx = ri.data.mouse.lLastX;
				m->dy = ri.data.mouse.lLastY;
			}
		}
	}

	return true;
}

static inline u8*	SysReadOnlyMemMapFile( const char* file )
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
static inline void	SysCloseMemMapFile( void* mmFile )
{
	UnmapViewOfFile( mmFile );
	//mmFile = 0;
}

LRESULT CALLBACK MainWndProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch( uMsg )
	{
	case WM_CLOSE: case WM_DESTROY:  PostQuitMessage( 0 ); break;
	}
	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}						 

#include "imgui/imgui.h"

INT WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, INT )
{
	using namespace DirectX;

	WIN_CHECK( !XMVerifyCPUSupport() );

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
	wc.lpszClassName = ENGINE_NAME;
	WIN_CHECK( !RegisterClassEx( &wc ) );
	

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
	WIN_CHECK(  !hWnd );

	ShowWindow( hWnd, SW_SHOWDEFAULT );


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
	WIN_CHECK( !RegisterRawInputDevices( hid, POPULATION( hid ), sizeof( RAWINPUTDEVICE ) ) );


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

	// TODO: use store/load ?
	XMVECTOR camFwdBasis = XMVectorSet( 0, 0, 1, 0 );
	XMVECTOR camUpBasis = XMVectorSet( 0, 1, 0, 0 );
	XMFLOAT3 camWorldPos = { 0,0,0 };
	
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

		XMMATRIX xmProjView = XMMatrixMultiply( XMLoadFloat4x4A( &frameData.activeView ), proj );
		XMStoreFloat4x4A( &frameData.projView, xmProjView );

		frameData.elapsedSeconds = elapsedSecs;
		frameData.freezeMainView = kbd.f;
		frameData.dbgDraw = kbd.o;

		ImGui::NewFrame();

		if( kbd.esc )
		{
			ImGui::ShowDemoWindow();
		}

		ImGui::Render();
		ImGui::EndFrame();

		HostFrames( frameData );
	}

	VkBackendKill();
	VirtualFree( sysMem, 0, MEM_RELEASE );
	SysOsKillConsole();

	return 0;
}

