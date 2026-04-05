#pragma once

#ifndef __SYS_OS_API_H__
#define __SYS_OS_API_H__

#include "ht_core_types.h"

#include <cstdarg>
#include <vector>
#include <memory>

//////////////////////////////////////
// MACROS
//////////////////////////////////////


#ifdef _WIN32
//////////////////////////////////////
//  WRITE WIN ABSTRACTIONS HERE
// TO AVOID include WIN every where
//////////////////////////////////////

#endif // _WIN32


struct frame_data;
struct mesh_upload_req;
struct virtual_arena;

#include "engine_types.h"
//////////////////////////////////////
// CONSTS
//////////////////////////////////////
constexpr u32 SCREEN_WIDTH = 1024;
constexpr u32 SCREEN_HEIGHT = 640;

//////////////////////////////////////
// ENGINE -> PLATFORM
//////////////////////////////////////
struct renderer_interface
{
	virtual void InitBackend( uintptr_t hInst, uintptr_t hWnd ) = 0;
	virtual HRNDMESH32 AllocMeshComponent() = 0;
	virtual void UploadMeshes( std::span<const mesh_upload_req> meshAssets, virtual_arena& arena ) = 0;
	virtual void HostFrames( const frame_data& frameData, gpu_data& gpuData ) = 0;
};

std::unique_ptr<renderer_interface> MakeRenderer();

//////////////////////////////////////
// PLATFORM -> ENGINE
//////////////////////////////////////
u64			SysGetCpuFreq();
u64			SysTicks();
void		SysErrMsgBox( const char* str );
// FILE API--------------------------
std::vector<u8> SysReadFile( const char* fileName );


#endif // !__SYS_OS_API_H__