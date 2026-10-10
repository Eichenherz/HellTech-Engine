#pragma once

#ifndef HELLTECH_SYS_IO_RING_H
#define HELLTECH_SYS_IO_RING_H
#include <span>

#include <ht_core_types.h>

// TODO: handle multiple files and buffers better !
void* SysMakeIoRing( u64 maxRequests, std::span<void *const> hFiles, std::span<const std::span<u8>> readBuffers );

bool SysTryPushIoRingRequest(
    void*   hIORing,
    u64     fileIdx,
    u64     readBuffIdx,
    u32     readBuffOffset,
    u64     fileOffset,
    u32     readSize );

u64 SysIoRingSubmit( void* hIORing, u64 waitInMs = 1 );

bool SysIoRingPollCompletion( void* hIORing, u32 completionIdx );

#endif //!HELLTECH_SYS_IO_RING_H