#pragma once

#ifndef HELLTECH_SYS_IO_RING_H
#define HELLTECH_SYS_IO_RING_H
#include <span>

#include <ht_core_types.h>

// TODO: handle multiple files and buffers
void* SysMakeIoRing( u64 maxRequests, void* hFile, std::span<const std::span<u8>> readBuffers );

bool SysTryPushIoRingRequest(
    void*   hIORing,
    u64     readBuffIdx,
    u32     readBuffOffset,
    u64     fileOffset,
    u32     readSize );

// NOTE: this is intended for one off/rare stuff, using an unregistered buffer is costly
bool SysTryPushIoRingRequestWRawBuffer( void* hIORing, void* rawBuffer, u64 fileOffset, u32 readSize );

u64 SysIoRingSubmit( void* hIORing, u64 waitInMs = 1 );

bool SysIoRingPollCompletion( void* hIORing );

#endif //!HELLTECH_SYS_IO_RING_H