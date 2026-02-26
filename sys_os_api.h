#pragma once

#ifndef __SYS_OS_API_H__
#define __SYS_OS_API_H__

#include "core_types.h"

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

// TODO: move
struct gpu_data
{
	float timeMs;
};


struct frame_data;
struct hellpack_view;
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
	virtual void UploadAsync( const hellpack_view& hellpackView ) = 0;
	virtual void HostFrames( const frame_data& frameData, gpu_data& gpuData ) = 0;
};

std::unique_ptr<renderer_interface> MakeRenderer();

void		CoreLoop();

//////////////////////////////////////
// PLATFORM -> ENGINE
//////////////////////////////////////
u64			SysGetCpuFreq();
u64			SysTicks();
u64			SysDllLoad( const char* name );
void		SysDllUnload( u64 hDll );
void*		SysGetProcAddr( u64 hDll, const char* procName );
void		SysDbgPrint( const char* str );
void		SysErrMsgBox( const char* str );
// FILE API--------------------------
std::vector<u8> SysReadFile( const char* fileName );
u64			SysGetFileTimestamp( const char* filename );
bool		SysWriteToFile( const char* filename, const u8* data, u64 sizeInBytes );

struct sys_window
{
	//virtual void SetUserData( uintptr_t pData ) = 0;
};

#endif // !__SYS_OS_API_H__