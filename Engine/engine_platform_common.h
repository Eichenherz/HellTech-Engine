#pragma once

#ifndef __HELLTECH_ENGINE_PLATFORM_COMMON_H__
#define __HELLTECH_ENGINE_PLATFORM_COMMON_H__

//////////////////////////////////////
// ENGINE -> PLATFORM
//////////////////////////////////////

#include <ht_core_types.h>
#include "engine_types.h"

#include <bitset>

// TODO: maybe make ht_engine_systems.h ?
#include "ht_ring_buffer.h"
#include "ht_stretchybuff.h"

struct virtual_arena;
struct sys_semaphore;
// TODO: must patch shaders to take any resolution
constexpr u32 SCREEN_WIDTH = 1024;
constexpr u32 SCREEN_HEIGHT = 640;

struct ht_input_state
{
    static constexpr u64 BUTTON_COUNT = 0x205; // TODO: maybe not here
    // NOTE: includes mouse buttons
    std::bitset<BUTTON_COUNT>   buttonsEndedDown = {};
    u16                         buttonsHalfTransitions[ BUTTON_COUNT ] = {};
    i32 	                    mouseDx = {};
    i32 	                    mouseDy = {};
    float2 	                    mousePos = {};

    inline bool IsButtonDown( u16 buttonId ) const
    {
        return buttonsEndedDown[ buttonId ];
    }

    inline bool IsButtonPressed( u16 buttonId ) const
    {
        return buttonsEndedDown[ buttonId ] && ( buttonsHalfTransitions[ buttonId ] & 1 );
    }
    inline bool IsButtonReleased( u16 buttonId ) const
    {
        return !buttonsEndedDown[ buttonId ] && ( buttonsHalfTransitions[ buttonId ] & 1 );
    }
    inline bool IsButtonHeld( u16 buttonId ) const
    {
        return buttonsEndedDown[ buttonId ] && ( 0 == buttonsHalfTransitions[ buttonId ] );
    }
    inline bool IsButtonIdle( u16 buttonId ) const
    {
        return !buttonsEndedDown[ buttonId ] && ( 0 == buttonsHalfTransitions[ buttonId ] );
    }

    inline void UpdateButtonState( u16 buttonId, const bool keyPressed )
    {
        buttonsHalfTransitions[ buttonId ] += ( keyPressed == buttonsEndedDown[ buttonId ] ) ? 0 : 1;
        buttonsEndedDown[ buttonId ] = keyPressed;
    }
};

inline ht_input_state HTReinitInputState( const ht_input_state& inputState )
{
    return { .buttonsEndedDown = inputState.buttonsEndedDown, .mousePos = inputState.mousePos };
}

using PFN_Job = void ( * )( void*, virtual_arena* );
struct job_t
{
    PFN_Job PfnJob;
    void*   payload;
};

struct job_system_ctx
{
    sys_semaphore		    sema;
    ringbuff_w_lock<job_t>	queue;

    job_system_ctx();

    void SubmitJob( job_t job );
};

struct renderer_interface
{
    virtual void		    InitBackend( u64 hInst, u64 hWnd ) = 0;
    virtual HRNDMESH32	    AllocMeshComponent( const hellpack_mesh_asset& ) = 0;
    virtual HJOBFENCE32     AllocJobFence() = 0;
    virtual bool            PollJobFenceAndRemoveOnCompletion( HJOBFENCE32 hJobFence, u64 timeoutNanosecs ) = 0;
    virtual void		    UploadMeshes( HJOBFENCE32, std::span<const mesh_upload_req>, virtual_arena& ) = 0;
    virtual void		    HostFrames( const frame_data&, virtual_arena&, gpu_data& ) = 0;
};

std::unique_ptr<renderer_interface> MakeRenderer();

constexpr char	ENGINE_NAME[] = "helltech_engine";
constexpr char	WINDOW_TITLE[] = "HellTech Engine";

struct helltech_interface
{
    // TODO: maybe place somewhere else
    virtual void Init( job_system_ctx* jobSystemCtx, u64 hInst, u64 hWnd, u16 width, u16 height ) = 0;
    virtual void RunLoop( double elapsedTime, bool isRunning, virtual_arena& scratchArena, const ht_input_state& inputState ) = 0;
};

helltech_interface* MakeHelltech( virtual_arena& arena );
//////////////////////////////////////
// PLATFORM -> ENGINE
//////////////////////////////////////

#endif //!__HELLTECH_ENGINE_PLATFORM_COMMON_H__