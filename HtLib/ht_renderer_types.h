#pragma once

#ifndef HT_RENDERER_TYPES_H
#define HT_RENDERER_TYPES_H

#include <ht_core_types.h>
#include <ht_vec_types.h>
#include <ht_macros.h>

//======================CONSTS======================//
// TODO: don't guess and actually get these numbers form the data; sure some of the numbers are gonna come off measurements
CONSTEXPR u64 MAX_PAYLOAD_SZ_IN_BYTES = 900 << 20;
CONSTEXPR u64 MAX_INSTANCES_IN_SCENE  = 1400 << 10;
CONSTEXPR u64 MAX_MESHES_IN_SCENE     = 16 << 10;
CONSTEXPR u64 MAX_MESHES_CAP          = 64 << 10;
CONSTEXPR u64 MAX_MESHLETS_IN_SCENE   = 1200 << 10;
//=================================================//

#define MAX_DESCRIPTOR_COUNT ( 1 << 16 )

struct view_data
{
	float4x4	proj;
	float4x4	mainView;
	float4x4	prevView;
	float4x4	mainViewProj;
	float4x4    prevViewProj;
	pkd_fp32x3  worldPos;
	float		zNear;
	pkd_fp32x3	camViewDir;
	float		lodTarget;
};

CONSTEXPR u32 BIT_DEPTH_OCT_N = 11;
CONSTEXPR u32 BIT_DEPTH_TAN_A = 9;
CONSTEXPR u32 BIT_DEPTH_BTN_S = 1;

typedef u32 oct16x2;

// NOTE: octahedron encoded normal + tan angle + bitan sign; will alias bc we will select the bit depth in the enc/dec
typedef u32 oct11x2s_a9_s1;

// NOTE: the positions will be given as a bit stream of variable len
struct packed_vtx_attr
{
	oct11x2s_a9_s1	encodedTBN;
	u16x2			encodedUVs;  // NOTE: use f16tof32 in the shader
};

struct gpu_instance
{
	float4x3	toWorld;
	pkd_fp32x3	scale;
	u32			meshIdx;
};

STATIC_ASSERT( 64 == sizeof( gpu_instance ), "Size mismatch!" );

CONSTEXPR u64 MAX_LOD_LEVELS_COUNT  = 4;
CONSTEXPR u64 MAX_MESHLETS_PER_MESH = ~u16( 0 );

// NOTE: we can pack x4 bc we know the exact count of LODs; this is tailored to 4 lods only !!!!

struct gpu_mesh_bounds
{
	pkd_fp32x4  lod4Err; // NOTE: monotonically increasing x -> w
	pkd_fp32x3	aabbMin; // NOTE: we only quantize the meshlet aabbs be they're the main "draw primitive"
	pkd_fp32x3	aabbMax;
};

struct gpu_mesh_lod
{
    u32 payloadStartInBytes; // NOTE: 4 GBs of geometry is plenty i guess
    u32 firstMlt;
    u32 mltCount;
    //u64 payloadStartIn8Bytes : 27;
    //u64 firstMlt : 21;
    //u64 mltCount : 16;
};

// TODO: we should prolly just use the hell_pack for this stuff and enforce it as a format basically ?
struct gpu_mesh
{
    pkd_fp32x4	    lod4Err; // NOTE: monotonically increasing x -> w
    pkd_fp32x3	    aabbMin; // NOTE: we only quantize the meshlet aabbs bc they're the main "draw primitive"
    pkd_fp32x3	    aabbMax;
    gpu_mesh_lod    lods[ MAX_LOD_LEVELS_COUNT ];
};

#ifndef __cplusplus
u32 GPUMeshGetLodMask( in gpu_mesh mesh )
{
    u32 resMask = 0;
    [unroll] for( u32 li = 0; li < MAX_LOD_LEVELS_COUNT; ++li )
    {
        u32x3 lodAsVec = u32x3( mesh.lods[ li ].payloadStartInBytes, mesh.lods[ li ].firstMlt, mesh.lods[ li ].mltCount );
        resMask |= ( ( u32 ) any( u32x3( 0, 0, 0 ) != lodAsVec ) ) << li;
    }
    return resMask;
}
#endif

CONSTEXPR u64 MAX_VTX_PER_MLT	= 64;
CONSTEXPR u64 MAX_TRIS_PER_MLT	= 128;
CONSTEXPR u64 MLT_MAX_INDEX		= 1ull << 12;
// NOTE: this 2 level LOD scheme is inspired by https://x.com/zeuxcg/status/1810841187433205817

// NOTE: the payload will be LOCAL and laid as [ pos ][ attrs ][ idxLOD0 ][ idxLOD1 ]
struct gpu_meshlet
{
	pkd_fp32x3	aabbMin;
	pkd_fp32x3	aabbMax;
    u64         offsetInPayload; // NOTE: always 4 bytes aligned
	u32		    packed8888_XYZ_Grid_BitDepth;
	// NOTE: packed as: lowest bytes - [ 8 VtxCount | 12 lod0_Idx | 12 lod_1_Idx ] - highest bytes
	u32		    packed8_12_12_VtxCount_Lod_01_IdxCount;
	float	    lodError;
};

#ifndef __cplusplus
u32x3 UnpackMeshletVtxAndLodIdxCount( in gpu_meshlet mlt )
{
	u32		packed_8_12x2	= mlt.packed8_12_12_VtxCount_Lod_01_IdxCount;
	u32x3	unpackShifted	= { packed_8_12x2, packed_8_12x2 >> 8, packed_8_12x2 >> 20 }; // 0, + 8, + 12
	u32		lodMask			= ( 1u << 12 ) - 1;
	return unpackShifted & u32x3( 0xff, lodMask, lodMask );
}

u32x2 GPUMeshletComputeVtxStreamSizes( in gpu_meshlet mlt )
{
    u32x4   xyz_Grid_BitDepth   = unpack_u8u32( mlt.packed8888_XYZ_Grid_BitDepth );
    u64     bitsPerPos          = xyz_Grid_BitDepth.x + xyz_Grid_BitDepth.y + xyz_Grid_BitDepth.z;
    u64     vtxCount            = mlt.packed8_12_12_VtxCount_Lod_01_IdxCount & 0xff;
    u64     posBitstreamSzBytes = ( ( bitsPerPos * vtxCount + 63 ) / 64 ) * 8;

    return u32x2( posBitstreamSzBytes, sizeof( oct16x2 ) * vtxCount );
}
#endif

STATIC_ASSERT( 48 == sizeof( gpu_meshlet ), "Size mismatch!" );
STATIC_ASSERT( ( 1ull << 12) == MLT_MAX_INDEX, "The max mltidx convention is broken" );

struct dispatch_command
{
#if defined( __cplusplus ) && defined( __VK )
	VkDispatchIndirectCommand cmd;
#else
	u32 localSizeX;
	u32 localSizeY;
	u32 localSizeZ;
#endif
};

struct draw_meshlet_command
{
#if defined( __cplusplus ) && defined( __VK )
	VkDrawIndexedIndirectCommand cmd;
#else
	u32    indexCount;
	u32    instanceCount;
	u32    firstIndex;
	u32    vertexOffset;
	u32    firstInstance;
#endif
};

struct draw_meshlet_cmd_data
{
    u32 meshPayloadStartInBytes;
	u32 globalInstId    : 28;
	u32 lodIdx          : 4;
	u32 globalMltId     : 28;
    u32 padding         : 4;
};

struct draw_instanced_indexed_indirect
{
#if defined( __cplusplus ) && defined( __VK )
	VkDrawIndexedIndirectCommand cmd;
#else
	u32    indexCount;
	u32    instanceCount;
	u32    firstIndex;
	u32    vertexOffset;
	u32    firstInstance;
#endif
};

struct downsample_info
{
	float2	invRes;
	u32		mips;
	u32		workGroupCount;
};

struct avg_luminance_info
{
	float minLogLum;
	float invLogLumRange;
	float dt;
};

struct dbg_vertex
{
	pkd_fp32x3 pos;
};

struct dbg_aabb_instance
{
	float4x4	toWorld; // NOTE: 4x4 so we can cram in the frustum transfrom too
	float4		color;
	pkd_fp32x3	minAabb;
	pkd_fp32x3	maxAabb;
};

struct luminance_histogram
{
	u32 finalLumSum;
	u32 finalTailValsCount;
};

struct visible_instance
{
    u32 meshPayloadStartInBytes;
	u32 instId : 28;
	u32 lodIdx : 4;
	u32 meshletOffset;
	u32 meshletCount;
};

struct meshlet_cull_wok_item
{
    u32 meshPayloadStartInBytes;
	u32 instId : 28;
	u32 lodIdx : 4;
	u32 globMltId;
};

struct culling_pass_params
{
    u64 visibleInstAddr;
    u64 visibleInstCounterAddr;
    u64 occludedInstAddr;
    u64 occludedInstCounterAddr;

    u64 mltToTestAddr;
    u64 mltToTestCounterAddr;
    u64 occludedMltAddr;
    u64 occludedMltCounterAddr;

    u64 dispatchCmdsAddr;
    u64 drawCmdsAddr;
    u64 drawDataAddr;
    u64 drawCounterAddr;

    u32 hizTexIdx;
    u32 hizSamplerIdx;

    u32 instCount;
    u32 isLatePass;
    u32 instCullWorkGrX;

    // DBG
    u32 enableInstCull;
    u32 enableMltCull;
    u32 enableMeshLod;
    u32 enableMltLod;
};

struct vbuffer_params
{
	u64 drawDataAddr;
};

struct depth_prepass_params
{
	u64 drawDataAddr;
};
struct meshlet_pass_params
{
    u64 drawDataAddr;
};

// NOTE: src and dst assumed to be the same dimensions, asserted on the host
struct vbuffer_dbg_draw_params
{
	u32x2	vbuffRes;
	u32 	srcIdx;
	u32 	dstIdx;
};

struct lambertian_clay_params
{
	u32x2	vbuffRes;
	u32		vbuffIdx; // NOTE: asserted in the renderer that they're eq size
	u32		dstIdx;
};

struct dbg_box_params
{
	u64 instBuffAddr;
	u64 vtxBuffAddr;
	u32 camIdx;
};

struct record_dbg_draw_params
{
	u64 gpuInstCountAddr;
	u64 dbgDrawCmdsAddr;
	u64 dbgDrawCountAddr;
	// TODO: use a dbg obj table or smth
	u32 indexCount;
	u32 firstIndex;
	u32 vertexOffset;
};

CONSTEXPR u32x2  MIP0_TILE_SIZE = { 32, 32 };

struct downsampler_params
{
	u32x2   srcResolution;
	u32x2	mip0Resolution;
	// u32x2 numWorkgroups; // NOTE: only for DX12-hlsl
	u32     mipCount;
	u32     reductionSamplerIdx;
	u32		srcSrvIdx;
	u32		dstMipsIdx[ 12 ];
	u32		atomicWgCounterIdx;
	u32     isMip0FromNonPot;
};

struct multi_pass_downsampler_params
{
	u32x2	srcSize;
	u32x2	dstSize;
	u32     reductionSamplerIdx;
	u32		pointSamplerIdx;
	u32     inImgIdx;
	u32     inImgLod;
	u32     outImgIdx;
	u32     isMip0FromNonPot;
};

struct indirect_dispatcher_params
{
    u64 dispatchCmdBuffAddr;
    u64 counterBufferAddr;
    u32 cullShaderWorkGrX;
};

struct imgui_vertex
{
    float   x, y;
    float   u, v;
    u32     rgba8Unorm;
};

struct imgui_pass_args
{
    float2  scale;
    float2  translate;
    u64     vtxAddr;
    u32     texAtlasIdx;
    u32     samplerIdx;
};

struct global_data
{
	u64 mltAddr;
	u64 pldAddr;
	u64 meshDescAddr;
    u64 meshRefAddr;
	u64 instAddr;
    u64 viewsAddr;
    u64 lodRequestsAddr;

    u32 mainCamIdx;
};

CONSTEXPR u64 GLOB_DATA_BINDING_SLOT = 0;

#endif // !HT_RENDERER_TYPES_H
