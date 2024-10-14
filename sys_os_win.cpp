#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>


#include <fileapi.h>

#include <strsafe.h>

#include <assert.h>
#include <stdlib.h>




#include <charconv>
#include <string>
#include <algorithm>

#include "sys_os_api.h"
#include "core_types.h"


#include "r_data_structs.h"





// PLATOFRM_FILE_API
// TODO: char vs wchar
static inline u32 SysGetFileAbsPath( const char* fileName, char* buffer, u64 buffSize )
{
	static_assert( sizeof( DWORD ) == sizeof( u32 ) );
	return GetFullPathNameA( fileName, buffSize, buffer, 0 );
}








static bool frustumCullDbg = 0;

enum class win32_vk : u16
{
	W = 0x57,
	A = 0x41,
	S = 0x53,
	D = 0x44,
	C = 0x43,
	F = 0x46,
	O = 0x4F,
	SPACE = VK_SPACE,
	ESC = VK_ESCAPE,
	CTRL = VK_CONTROL 
};


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


