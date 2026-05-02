#pragma once

#ifndef __HELLTECH_ENGINE_PLATFORM_COMMON_H__
#define __HELLTECH_ENGINE_PLATFORM_COMMON_H__

//////////////////////////////////////
// ENGINE -> PLATFORM
//////////////////////////////////////

#include <ht_core_types.h>
#include "engine_types.h"

// TODO: maybe make ht_engine_systems.h ?
#include "ht_mtx_queue.h"
#include "ht_stretchybuff.h"

struct virtual_arena;
struct sys_semaphore;

constexpr u32 SCREEN_WIDTH = 1024;
constexpr u32 SCREEN_HEIGHT = 640;

#include <bitset>

struct input_state
{
    std::bitset<512>    keyStates;
    std::bitset<5>      mouseButtons;
    i32 	            mouseDx;
    i32 	            mouseDy;
    float2 	            mousePos;
};

using PFN_Job = void ( * )( void*, virtual_arena* );
struct job_t
{
    PFN_Job PfnJob;
    void*   payload;
};

struct job_system_ctx
{
    sys_semaphore		sema;
    mtx_queue<job_t>	queue;

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
    virtual void		    HostFrames( const frame_data&, gpu_data& ) = 0;
};

std::unique_ptr<renderer_interface> MakeRenderer();

constexpr char	ENGINE_NAME[] = "helltech_engine";
constexpr char	WINDOW_TITLE[] = "HellTech Engine";

struct helltech_interface
{
    // TODO: maybe place somewhere else
    virtual void Init( job_system_ctx* jobSystemCtx, u64 hInst, u64 hWnd, u16 width, u16 height ) = 0;
    virtual void RunLoop( double elapsedTime, bool isRunning, virtual_arena& scratchArena, const input_state& inputState ) = 0;
};

helltech_interface* MakeHelltech( virtual_arena& arena );
//////////////////////////////////////
// PLATFORM -> ENGINE
//////////////////////////////////////

#endif //!__HELLTECH_ENGINE_PLATFORM_COMMON_H__