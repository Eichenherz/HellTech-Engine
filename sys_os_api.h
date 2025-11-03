#pragma once

#include "core_types.h"

#include <vector>

#include "ht_error.h"

#include "ht_utils.h"
//////////////////////////////////////
// MACROS
//////////////////////////////////////

#define BYTE_COUNT( buffer ) (u64) std::size( buffer ) * sizeof( buffer[ 0 ] )


#ifdef _WIN32
//////////////////////////////////////
//  WRITE WIN ABSTRACTIONS HERE
// TO AVOID include WIN every where
//////////////////////////////////////

#endif // _WIN32

// TODO: move
struct gpu_data
{
	float timeMs;
};


struct frame_data;

//////////////////////////////////////
// CONSTS
//////////////////////////////////////
constexpr u32 SCREEN_WIDTH = 1024;
constexpr u32 SCREEN_HEIGHT = 640;
constexpr u64 SYS_MEM_BYTES = 1 * GB;

// TODO: remove extern ? 
//////////////////////////////////////
// ENGINE -> PLATFORM
//////////////////////////////////////
void			CoreLoop();

extern void		VkBackendInit( uintptr_t hInst, uintptr_t hWnd );
extern void		HostFrames( const frame_data& frameData, gpu_data& gpuData );
extern void		VkBackendKill();
extern void     Dx12BackendInit();
//////////////////////////////////////
// PLATFORM -> ENGINE
//////////////////////////////////////
extern u64		SysGetCpuFreq();
extern u64		SysTicks();
extern u64		SysDllLoad( const char* name );
extern void		SysDllUnload( u64 hDll );
extern void*	SysGetProcAddr( u64 hDll, const char* procName );
extern void		SysDbgPrint( const char* str );
extern void		SysErrMsgBox( const char* str );
// FILE API--------------------------
extern u32		SysGetFileAbsPath( const char* fileName, char* buffer, u64 buffSize );
extern std::vector<u8> SysReadFile( const char* fileName );
extern u64		SysGetFileTimestamp( const char* filename );
extern bool		SysWriteToFile( const char* filename, const u8* data, u64 sizeInBytes );

struct sys_window
{
	//virtual void SetUserData( uintptr_t pData ) = 0;
};