#ifndef __SYS_PLATFORM__
#define __SYS_PLATFORM__

#include "core_types.h"

u64	SysGetCpuFreq();
u64	SysTicks();

bool SysPumpPlatfromMessages();

u64	SysDllLoad( const char* name );
void SysDllUnload( u64 hDll );


void* SysGetProcAddr( u64 hDll, const char* procName );
void SysDbgPrint( const char* str );
void SysErrMsgBox( const char* str );

struct sys_window
{
	virtual void SetUserData( uintptr_t pData ) = 0;
};

#endif // !__SYS_PLATFORM__
