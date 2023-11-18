#pragma once
#include "core_types.h"
// TODO: rename file

////////////////////////////////////////////
/// MEM LAYOUT =============================
// SCRATCH PAD -> || ASSETS || we'll see
//  LNR + TEMP == ||  LNR ? ||
////////////////////////////////////////////






constexpr u64 DEFAULT_ALIGNMNET = 2 * sizeof( void* );

struct mem_arena
{
	u8*		mem;
	u64		size;
	u64		currOffset;
};

// TODO: should not expose detail, too low level
struct temp_arena
{
	mem_arena*	arena;
	u64			currOffset;
};
 
extern u64	FwdAlign( u64 addr, u64 alignment );
bool		MemSysInit( u8* baseAddr, u64 size );
void		ArenaInit( mem_arena* a, u8* backBuffer, u64 backBuffLen );
u8*			LinearAlignAlloc( mem_arena* a, u64 size, u64 alignment = DEFAULT_ALIGNMNET );
void		LinearReset( mem_arena* a );
temp_arena	TempArenaBegin( mem_arena* a );
void		TempArenaEnd( temp_arena temp );

extern mem_arena scratchPad;
extern mem_arena primaryMem;
extern mem_arena debugMem;