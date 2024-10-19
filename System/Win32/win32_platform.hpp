#ifndef __WIN32_PLATFORM__
#define __WIN32_PLATFORM__

#include <System/sys_platform.hpp>
#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

struct win32_window : sys_window
{
	HINSTANCE hinstance;
	HWND hwnd;

	// NOTE: this is stoopid
	win32_window( HINSTANCE hinstance, HWND hwnd )
	{
		this->hinstance = hinstance;
		this->hwnd = hwnd;
	}

	inline virtual void SetUserData( uintptr_t pData ) override;
};

#endif // !__WIN32_PLATFORM__
