#pragma once

#ifndef __SYS_SYNC_H__
#define __SYS_SYNC_H__

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

#endif // !__SYS_SYNC_H__

