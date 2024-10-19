#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#include <hidusage.h>
#include <iostream>
#include <io.h>
#include <shlwapi.h>
#include <fcntl.h>

#include <memory>
#include <assert.h>

#include "win32_utils.hpp"
#include "win32_platform.hpp"
#include "helltech.hpp"

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

static inline RAWINPUT Win32GetRawInput( LPARAM lParam )
{
	RAWINPUT ri = {};
	u32 rawDataSize = sizeof( ri );
	// TODO: how to handle GetRawInputData errors ?
	UINT res = GetRawInputData( ( HRAWINPUT )lParam, RID_INPUT, &ri, &rawDataSize, sizeof( RAWINPUTHEADER ) );
	WIN_CHECK( res == UINT( -1 ) );

	return ri;
}

static LRESULT CALLBACK MainWndProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	input_manager* pInputManager = reinterpret_cast<input_manager*>( GetWindowLongPtr( hwnd, GWLP_USERDATA ) );

	switch( uMsg )
	{
	case WM_CLOSE: case WM_DESTROY:  PostQuitMessage( 0 ); break;
	case WM_SETFOCUS:
		pInputManager->hasFocus = true;
		break;
	case WM_KILLFOCUS: 
	{ // NOTE: reset the keys, but keep the mouse position
			pInputManager->hasFocus = false;
			pInputManager->kbd.ClearState();
			break;
	}
	//case WM_QUIT:
	case WM_INPUT:
	{
		if( pInputManager->hasFocus )
		{
			const RAWINPUT ri = Win32GetRawInput( lParam );
			if( ri.header.dwType == RIM_TYPEKEYBOARD )
			{
				const RAWKEYBOARD& rawKbd = ri.data.keyboard;
				const bool isReleased = rawKbd.Flags & RI_KEY_BREAK;
				//const bool isPressed = rawKbd.Flags & RI_KEY_MAKE; RI_KEY_MAKE == 0 

				const bool isE0 = rawKbd.Flags & RI_KEY_E0;
				const bool isE1 = rawKbd.Flags & RI_KEY_E1;

				// TODO: how to handle left & right CTRL
				//if( ri.data.keyboard.VKey == VK_CONTROL && !isE0 ) kbd->lctrl = isPressed;
				pInputManager->ProcessEvent( keyboard_handler::message{ .vk = ( virtual_key ) rawKbd.VKey, .pressed = !isReleased } );

			}
			else if( ri.header.dwType == RIM_TYPEMOUSE )
			{
				const RAWMOUSE& rawMouse = ri.data.mouse;
				const bool lmbDown = rawMouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN;
				const bool lmbUp = rawMouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP;
				const bool rmbDown = rawMouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN;
				const bool rmbUp = rawMouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN;

				if( rawMouse.usButtonFlags ) // if pressed/released basically
				{
					// NOTE: sanity check
					assert( lmbDown ^ lmbUp );
					assert( rmbDown ^ rmbUp );
				}
				// TODO: relative positions ?
				pInputManager->ProcessEvent( mouse_handler::message{ 
					.dx = ( float ) rawMouse.lLastX, .dy = ( float ) rawMouse.lLastY } );
			}
		}
		break;
	}
	}
	return DefWindowProc( hwnd, uMsg, wParam, lParam );
}	



#include "directx_math.hpp"
#include <algorithm>
using namespace DirectX;

struct virtual_camera
{
	static constexpr DirectX::XMFLOAT3 fwdBasis = { 0, 0, 1 };
	static constexpr DirectX::XMFLOAT3 upBasis = { 0, 1, 0 };

	DirectX::XMMATRIX projection;

	DirectX::XMFLOAT3 worldPos = { 0,0,0 };
	// NOTE: pitch must be in [-pi/2,pi/2]
	float pitch = 0;
	float yaw = 0;

	float speed = 1.5f;

	inline void Move( DirectX::XMVECTOR camMove, DirectX::XMFLOAT2 dPos, float elapsedSecs )
	{
		XMMATRIX tRotScale = XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) * XMMatrixScaling( speed, speed, speed );
		camMove = XMVector3Transform( XMVector3Normalize( camMove ), tRotScale );

		XMVECTOR xmCamPos = XMLoadFloat3( &worldPos );
		XMVECTOR smoothNewCamPos = XMVectorLerp( xmCamPos, XMVectorAdd( xmCamPos, camMove ), 0.18f * elapsedSecs / 0.0166f );

		// TODO: thresholds
		//float moveLen = XMVectorGetX( XMVector3Length( smoothNewCamPos ) );
		XMStoreFloat3( &worldPos, smoothNewCamPos );

		yaw = XMScalarModAngle( yaw + dPos.x * elapsedSecs );
		pitch = std::clamp( pitch + dPos.y * elapsedSecs, -almostPiDiv2, almostPiDiv2 );
	}
	inline DirectX::XMVECTOR LookAt() const
	{
		return XMVector3Transform( XMLoadFloat3( &fwdBasis ), XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
	}
	inline DirectX::XMMATRIX ViewMatrix() const
	{
		auto lookAt = this->LookAt();
		XMVECTOR xmWorldPos = XMLoadFloat3( &worldPos );
		XMMatrixLookAtLH( xmWorldPos, XMVectorAdd( xmWorldPos, lookAt ), XMLoadFloat3( &upBasis ) );
	}
	inline DirectX::XMMATRIX FrustumMatrix() const
	{
		// NOTE: inv( A * B ) = inv B * inv A
		XMMATRIX invFrustMat = XMMatrixMultiply( ViewMatrix(), projection );
		XMVECTOR det = XMMatrixDeterminant( invFrustMat );
		return XMMatrixInverse( &det, invFrustMat );
	}
};

static bool frustumCullDbg = 0;

static INT WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, INT )
{
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
		.left = CW_USEDEFAULT,
		.top = CW_USEDEFAULT,
		.right = ( LONG ) SCREEN_WIDTH + wr.left,
		.bottom = ( LONG ) SCREEN_HEIGHT + wr.top
	};
	constexpr DWORD windowStyle = WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU;
	AdjustWindowRect( &wr, windowStyle, 0 );
	HWND hWnd = CreateWindow(
		wc.lpszClassName, WINDOW_TITLE, windowStyle, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, 0, 0, hInst, 0 );
	WIN_CHECK(  !hWnd );

	ShowWindow( hWnd, SW_SHOWDEFAULT );


	RAWINPUTDEVICE hid[] = {
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
	WIN_CHECK( !RegisterRawInputDevices( hid, ( UINT ) std::size( hid ), sizeof( RAWINPUTDEVICE ) ) );

	win32_window win32Wnd = { hInst, hWnd };

	helltech theHellTechEngine = { &win32Wnd };
	
	win32Wnd.SetUserData( reinterpret_cast<uintptr_t>( &theHellTechEngine.pInputManager ) );
	
	WIN_CHECK( !DirectX::XMVerifyCPUSupport() );

	
	virtual_camera mainCam;

	constexpr float zNear = 0.5f;
	constexpr float fovY = 70.0f;
	mainCam.projection = PerspectiveReverseZInfiniteZFar( 
		XMConvertToRadians( fovY ), float( SCREEN_WIDTH ) / float( SCREEN_HEIGHT ), zNear );

	gpu_data gpuData = {};
	frame_data frameData = {};

	

	theHellTechEngine.CoreLoop();

	return 0;
}