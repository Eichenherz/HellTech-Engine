#pragma once

#ifndef __SYS_SYNC_H__
#define __SYS_SYNC_H__

#include "ht_core_types.h"

#ifdef _MSC_VER

#include <concurrencysal.h>

#define ACQ_EXCL_LOCK _Acquires_exclusive_lock_( *this )
#define REL_EXCL_LOCK _Releases_exclusive_lock_( *this )

#endif


struct copyable_srwlock
{
    mutable void*       osLock;

                        copyable_srwlock();
                        copyable_srwlock( const copyable_srwlock& );
                        copyable_srwlock( copyable_srwlock&& );
    copyable_srwlock&   operator=( const copyable_srwlock& );
    copyable_srwlock&   operator=( copyable_srwlock&& );

    ACQ_EXCL_LOCK  void lock()   const;
    REL_EXCL_LOCK  void unlock() const;
};

enum sys_thread_signal : i64
{
    SYS_THREAD_SIGNAL_SLEEP		= 0,
    SYS_THREAD_SIGNAL_WAKEUP	= 1,
    SYS_THREAD_SIGNAL_EXIT		= -1
};

// NOTE: bc Win32 expects LONG64
#define ht_atomic64 volatile i64

void SysAtomicSignalSingleThread( ht_atomic64& signal, sys_thread_signal val );

// TODO: rethink
i64 SysAtomicWaitOnAddr( ht_atomic64& signal, void* undesiredVal, u32 millisecs );

struct sys_semaphore
{
    const u64 hndl;

    sys_semaphore();
};

static_assert( sizeof( sys_semaphore ) <= sizeof( u64 ) );

u32 SysSemaphoreRelease( sys_semaphore sema, u32 releaseVal );
void SysSemaphoreWait( sys_semaphore sema, u32 millisecs );

#endif // !__SYS_SYNC_H__

