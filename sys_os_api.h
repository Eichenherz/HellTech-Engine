#pragma once

#include "core_types.h"

//////////////////////////////////////
// MACROS
//////////////////////////////////////
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STR STRINGIZE(__LINE__)

#define RUNTIME_ERR_LINE_FILE_STR ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__

// NOTE: doesn't work with arrays passed as pointers in args
#define POPULATION( arr ) (u64) sizeof( arr )/ sizeof( arr[0] )

#define GB (u64)( 1 << 30 );
#define KB (u64)( 1 << 10 )

#ifdef _WIN32
//////////////////////////////////////
//  WRITE WIN ABSTRACTIONS HERE
// TO AVOID include WIN every where
//////////////////////////////////////


#endif // WIN32

struct cam_frustum;
struct global_data;
namespace std
{
	template<class, class> class vector;
}

//////////////////////////////////////
// CONSTS
//////////////////////////////////////
constexpr u32 SCREEN_WIDTH = 960;
constexpr u32 SCREEN_HEIGHT = 600;
constexpr u64 SYS_MEM_BYTES = 1 * GB;

//////////////////////////////////////
// ENGINE -> PLATFORM
//////////////////////////////////////
void			CoreLoop();

extern void		VkBackendInit();
extern void		HostFrames( const global_data* globs, const cam_frustum& camFrust, b32 bvDraw, float dt );
extern void		VkBackendKill();

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
extern u32		SysGetFileAbsPath( const char* fileName, char* buffer, u64 buffSize );
extern u8*		SysReadOnlyMemMapFile( const char* file );
extern void		SysCloseMemMapFile( void* mmFile );
extern std::vector<u8> SysReadFile( const char* fileName );
extern u64		SysGetFileTimestamp( const char* filename );