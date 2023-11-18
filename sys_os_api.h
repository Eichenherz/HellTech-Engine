#pragma once

#include "core_types.h"
#include <vector>
//////////////////////////////////////
// MACROS
//////////////////////////////////////
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STR STRINGIZE(__LINE__)

#define RUNTIME_ERR_LINE_FILE_STR ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__

// NOTE: doesn't work with arrays passed as pointers in args
#define POPULATION( arr ) (u64) sizeof( arr )/ sizeof( arr[ 0 ] )

#define BYTE_COUNT( buffer ) (u64) std::size( buffer ) * sizeof( buffer[ 0 ] )

#define GB (u64)( 1 << 30 )
#define KB (u64)( 1 << 10 )
#define MB (u64) ( 1 << 20 )

#ifdef _WIN32
//////////////////////////////////////
//  WRITE WIN ABSTRACTIONS HERE
// TO AVOID include WIN every where
//////////////////////////////////////


#endif // WIN32

// TODO: move
struct gpu_data
{
	float timeMs;
};


struct frame_data;
namespace std
{
	template<class, class> class vector;
}

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

extern void		VkBackendInit();
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