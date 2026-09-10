#pragma once

#ifndef __HELLTECH_ENGINE_PLATFORM_API_H__
#define __HELLTECH_ENGINE_PLATFORM_API_H__

#include <ht_core_types.h>

//////////////////////////////////////
// PLATFORM -> ENGINE
//////////////////////////////////////
struct linear_arena;
struct sys_semaphore;

//////////////////////////////////////
// ENGINE -> PLATFORM
//////////////////////////////////////

#include "engine_types.h"

#include <bitset>
#include <array>

// TODO: maybe make ht_engine_systems.h ?
#include <ht_ring_buffer.h>


// TODO: must patch shaders to take any resolution
constexpr u32 SCREEN_WIDTH  = 1280;
constexpr u32 SCREEN_HEIGHT = 720;

struct ht_input_state
{
    static constexpr u64 BUTTON_COUNT = 517; // TODO: maybe not here
    // NOTE: includes mouse buttons
    std::bitset<BUTTON_COUNT>   buttonsEndedDown                        = {};
    u16                         buttonsHalfTransitions[ BUTTON_COUNT ]  = {};
    i32 	                    mouseDx                                 = 0;
    i32 	                    mouseDy                                 = 0;
    float2 	                    mousePos                                = {};

    bool IsButtonDown( u16 buttonId ) const { return buttonsEndedDown[ buttonId ]; }

    bool IsButtonPressed( u16 buttonId ) const
    {
        return buttonsEndedDown[ buttonId ] && ( buttonsHalfTransitions[ buttonId ] & 1 );
    }
    bool IsButtonReleased( u16 buttonId ) const
    {
        return !buttonsEndedDown[ buttonId ] && ( buttonsHalfTransitions[ buttonId ] & 1 );
    }
    bool IsButtonHeld( u16 buttonId ) const
    {
        return buttonsEndedDown[ buttonId ] && ( 0 == buttonsHalfTransitions[ buttonId ] );
    }
    bool IsButtonIdle( u16 buttonId ) const
    {
        return !buttonsEndedDown[ buttonId ] && ( 0 == buttonsHalfTransitions[ buttonId ] );
    }

    void UpdateButtonState( u16 buttonId, const bool keyPressed )
    {
        buttonsHalfTransitions[ buttonId ] += ( keyPressed == buttonsEndedDown[ buttonId ] ) ? 0 : 1;
        buttonsEndedDown[ buttonId ] = keyPressed;
    }
};

inline ht_input_state HTReinitInputState( const ht_input_state& inputState )
{
    return { .buttonsEndedDown = inputState.buttonsEndedDown, .mousePos = inputState.mousePos };
}

using PFN_Job = void ( * )( void*, linear_arena* );
struct job_t
{
    PFN_Job PfnJob;
    void*   payload;
};

struct alignas( 64 ) job_system_ctx
{
    sys_semaphore		    sema;
    ringbuff_w_lock<job_t>	queue;

    job_system_ctx();

    void SubmitJob( job_t job );
};

struct alignas( 64 ) thread_ctx
{
    // NOTE: bc we might need to deinterleave the allocs sometimes
    // NOTE: saw this one in RADDebugger
    std::array<linear_arena, 2> scratchArenas = {};
};

extern u64                      gNumCores;
extern job_system_ctx*          pJobSys;
extern thread_local thread_ctx* pThreadCtx;
// NOTE: this is used to hold the actual engine components; ie thread pool, renderer, etc
extern linear_arena*            pPersistentArena;
// NOTE: this is also mostly persistent, it's capped to hold all the game data;
// ie usable set ( streaming ) or the level ( if level based )
extern linear_arena*            pGameArena;
extern linear_arena*            pDebugArena;

u64 HtCurrentThreadIdx();

struct renderer_interface
{
    virtual void		    InitBackend( u64 hInst, u64 hWnd ) = 0;
    virtual HRNDMESH32	    AllocMeshComponent( const hpk_mesh_view& ) = 0;
    virtual bool            PollJobCompletion( atomic_u64* hJobDoneSignal ) = 0;
    virtual void		    UploadMeshes( atomic_u64*, std::span<const mesh_upload_req>, linear_arena& ) = 0;
    virtual void		    HostFrames( const frame_data&, linear_arena&, gpu_data& ) = 0;
};

renderer_interface* MakeRenderer( linear_arena& );

constexpr char ENGINE_NAME[]    = "helltech_engine";
constexpr char WINDOW_TITLE[]   = "HellTech Engine";

struct helltech_interface
{
    // TODO: maybe place somewhere else
    virtual void Init( u64 hInst, u64 hWnd, u16 width, u16 height ) = 0;
    virtual void RunLoop(
        double                  elapsedTime,
        bool                    isRunning,
        linear_arena&           scratchArena,
        const ht_input_state&   inputState
    ) = 0;
};

helltech_interface* MakeHelltech( linear_arena& arena );

#endif //!__HELLTECH_ENGINE_PLATFORM_COMMON_H__