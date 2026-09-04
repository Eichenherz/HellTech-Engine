#pragma once

#ifndef __HELLTECH_SYS_THREAD_H__
#define __HELLTECH_SYS_THREAD_H__

#include <ht_core_types.h>

struct sys_thread
{
    u64	hndl;
    u32	threadId;
};

#ifndef THREAD_CALLING_CONV
#define THREAD_CALLING_CONV
#endif

using PfnSysThreadProc = u32( THREAD_CALLING_CONV * )( void* );

sys_thread  SysCreateThread( u64 stackSize, PfnSysThreadProc ThreadProc, void* pData, const wchar_t* name );
void        SysThreadSleep( u32 milliSecs );
void        SysNameThread( u64 hThread, const wchar_t* name );

#endif //!__HELLTECH_SYS_THREAD_H__