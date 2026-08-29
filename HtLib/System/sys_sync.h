#pragma once

#ifndef __SYS_SYNC_H__
#define __SYS_SYNC_H__

#include "ht_core_types.h"

#ifdef _MSC_VER

#include <concurrencysal.h>

#define ACQ_EXCL_LOCK _Acquires_exclusive_lock_( *this )
#define REL_EXCL_LOCK _Releases_exclusive_lock_( *this )

#endif

// NOTE: we only allow it to be copyable SO we don't have to deal with TOO much bullshit in C++
// TODO: see if we can make it NON copyable without breaking PODs and trivial types
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


using atomic_u64 = volatile u64;

enum class sys_fence_t
{
    NONE,
    ACQ,
    REL,
    COUNT
};

template<sys_fence_t BARRIER>
u64 SysAtomicCas64( atomic_u64* pAddr, u64 exchange, u64 comparand );
template<sys_fence_t BARRIER>
u64 SysAtomicAnd64( atomic_u64* pAddr, u64 mask );
template<sys_fence_t BARRIER>
u64 SysAtomicAdd64( atomic_u64* pAddr, u64 value );
template<sys_fence_t BARRIER>
u64 SysAtomicRead64( atomic_u64* pAddr );
template<sys_fence_t BARRIER>
void SysAtomicWrite64( atomic_u64* pAddr, u64 value );

struct sys_semaphore
{
    const u64 hndl;

    sys_semaphore();
};

static_assert( sizeof( sys_semaphore ) <= sizeof( u64 ) );

u32 SysSemaphoreRelease( sys_semaphore sema, u32 releaseVal );
void SysSemaphoreWait( sys_semaphore sema, u32 millisecs );

#endif // !__SYS_SYNC_H__

