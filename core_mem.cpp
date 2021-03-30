#include "core_types.h"
#include "core_lib_api.h"
#include "sys_os_api.h"
#include <assert.h>

//#include <intrin.h>
//#include <immintrin.h>

static_assert( !( SYS_MEM_BYTES & ( SYS_MEM_BYTES - 1 ) ), "Sys mem must be pow2 !" );
// NOTE: bound to change
constexpr u64 PAD_BYTES = 150 * MB;
constexpr u64 PRIM_BYTES = 600 * MB;
constexpr u64 FRAME_BYTES = 50 * MB;
constexpr u64 DBG_BYTES = 200 * MB;
// NOTE: little endian ascii
constexpr u32 PAD_ID = 0x00444150;
constexpr u32 PRIM_ID = 0x4d495250;
constexpr u32 FRAME_ID = 0x4d415246;
constexpr u32 DBG_ID = 0x00474244;


mem_arena scratchPad = {};
mem_arena primaryMem = {};
mem_arena debugMem = {};

b32 MemSysInit( u8* baseAddr, u64 size )
{
	ArenaInit( &scratchPad, baseAddr, PAD_BYTES );
	ArenaInit( &primaryMem, baseAddr += scratchPad.size, PRIM_BYTES );
	// TODO: separate debug from retail
	ArenaInit( &debugMem, baseAddr += primaryMem.size, SYS_MEM_BYTES - ( PAD_BYTES + PRIM_BYTES ) );

	return ( (b32) scratchPad.mem & (b32) primaryMem.mem & (b32) debugMem.mem );
}

//struct mem_arena
//{
//	u8*		mem;
//	u64		size;
//	u64		currOffset;
//};

__forceinline b32 IsPowOf2( u64 addr )
{
	return !( addr & ( addr - 1 ) );
}
__forceinline static u64 FwdAlign( u64 addr, u64 alignment )
{
	assert( IsPowOf2( alignment ) );
	u64 mod = addr & ( alignment - 1 );
	return mod ? addr + ( alignment - mod ) : addr;
}


void ArenaInit( mem_arena* a, u8* backBuffer, u64 backBuffLen )
{
	a->mem = backBuffer;
	a->size = backBuffLen;
	a->currOffset = 0;
}
u8* LinearAlignAlloc( mem_arena* a, u64 size, u64 alignment )
{
	u64 currPtr = (u64) ( a->mem + a->currOffset );
	u64 relOffset = FwdAlign( currPtr, alignment );
	relOffset -= (u64) a->mem;

	u8* region = 0;
	if( relOffset + size <= a->size ){
		region = a->mem + relOffset;
		a->currOffset = relOffset + size;
	}
	return region;
}
//u8* LinearAlignResize( mem_arena* a )
//{
//
//}
void LinearReset( mem_arena* a )
{
	a->currOffset = 0;
}

//struct temp_arena
//{
//	mem_arena*	arena;
//	u64			currOffset;
//};
temp_arena TempArenaBegin( mem_arena* a )
{
	temp_arena temp;
	temp.arena = a;
	temp.currOffset = a->currOffset;
	return temp;
}
void TempArenaEnd( temp_arena temp )
{
	temp.arena->currOffset = temp.currOffset;
}

typedef u8* ( *Gen_Alloc )( mem_arena*, u64, u64 );
typedef void ( *Gen_Free )( mem_arena* );
