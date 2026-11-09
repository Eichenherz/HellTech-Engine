#define VK_NO_PROTOTYPES
#define __VK // NOTE: used to not include all the vk shit everywhere
#include <vulkan.h>

#include <Volk/volk.h>
#include <offsetAllocator.hpp>

#include <string.h>
#include <string_view>
#include <span>
#include <format>

#include <ht_core_types.h>
#include <ht_utils.h>
#include "engine_platform_api.h"

#include "vk_error.h"
#include "vk_resources.h"
#include "vk_sync.h"
#include "vk_utils.h"
#include "vk_context.h"

#include <ht_array.h>
#include <ht_fixed_string.h>
#include "ht_slot_array.h"

#include "engine_types.h"

#include <ht_math.h>
#include <imgui.h>

#include <System/sys_file.h>

#include <hell_pack.h>

using offset_alloc_t = OffsetAllocator::Allocation;

struct offset_allocator_t
{
	// NOTE: empty state, reset() always eats 1 node so maxAllocs must be >= 2 or the freelist idx underflows
	OffsetAllocator::Allocator	mAlloc = { 0, 2 };

	offset_allocator_t() = default;
	offset_allocator_t( u32 size, u32 maxAllocs = 128 * 1024 ) : mAlloc{ size, maxAllocs } {}

	offset_allocator_t( offset_allocator_t&& o ) : mAlloc{ MOV( o.mAlloc ) } {}
	offset_allocator_t& operator=( offset_allocator_t&& o )
	{
		if ( this != &o )
		{
			mAlloc.~Allocator();
			new ( &mAlloc ) OffsetAllocator::Allocator( MOV( o.mAlloc ) );
		}
		return *this;
	}

	offset_alloc_t		Alloc( u32 sizeInBytes )
	{
	    HT_ASSERT( sizeInBytes );
		OffsetAllocator::Allocation alloc = mAlloc.allocate( sizeInBytes );
		HT_ASSERT( OffsetAllocator::Allocation::NO_SPACE != alloc.offset );
		return alloc;
	}
	void				Free( offset_alloc_t alloc ) { mAlloc.free( alloc ); }
};

static vk_context* pVkCtx = nullptr;

//====================CONSTS====================//
constexpr u64 				MAX_FIF         = vk_renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED;
constexpr VkCullModeFlags   HT_CULL_MODE    = IS_WORLD_RH ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
constexpr VkFrontFace       HT_FRONT_FACE   = IS_WORLD_RH ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
//==============================================//

//====================CVARS=====================//

//==============================================//

//==============CONSTEXPR_SWITCH================//

//==============================================//

struct vk_gpu_frame_profiler
{
    vk_query_pool					    timestampQueryPool      = {};
    vk_query_pool					    pplnStatsQueryPool      = {};
    // NOTE: these are 2 per query so we need std::size * 2
	borrowed_array<vk_timestamp_query>  timedZonesQueries       = {};
	borrowed_array<vk_pipestats_query>  pipelineStatsQueries    = {};
	vk_buffer						    timestampQueryBuff      = {};
	vk_buffer						    pipelineStatsQueryBuff  = {};

	void ReadbackQueries( borrowed_array<ht_timed_zone>& timedGpuZones, borrowed_array<ht_pipeline_stats>& pipelinesStats )
	{
	    for( const vk_timestamp_query& vkTsQuery : timedZonesQueries )
	    {
	        timedGpuZones.push_back( ResolveTimestampQuery( vkTsQuery ) );
	    }
	    for( const vk_pipestats_query& vkPsQuery : pipelineStatsQueries )
	    {
	        pipelinesStats.push_back( ResolvePipelineStatsQuery( vkPsQuery ) );
	    }
	}

	void GPUReadAndResetQueryPools( vk_command_buffer& cmdBuff )
	{
		cmdBuff.CmdReadQueryPoolResults( timestampQueryPool, timestampQueryBuff,
			0, ( u32 ) std::size( timedZonesQueries ) * 2 );
		cmdBuff.CmdReadQueryPoolResults( pplnStatsQueryPool, pipelineStatsQueryBuff,
			0, ( u32 ) std::size( pipelineStatsQueries ) );

		cmdBuff.CmdResetQueryPool( timestampQueryPool );
		cmdBuff.CmdResetQueryPool( pplnStatsQueryPool );

		timedZonesQueries.resize( 0 );
		pipelineStatsQueries.resize( 0 );
	}

	ht_timed_zone ResolveTimestampQuery( const vk_timestamp_query& hQuery ) const
	{
		u32 startIdx	= hQuery.begIdx * timestampQueryPool.queryStrideInSlots;
		u32 endIdx		= hQuery.endIdx * timestampQueryPool.queryStrideInSlots;
		u64 endTs		= VkBufferHostView<u64>( timestampQueryBuff )[ endIdx ];
		u64 startTs		= VkBufferHostView<u64>( timestampQueryBuff )[ startIdx ];
		return {
			.name	= hQuery.name,
			.timeMs = float( endTs - startTs ) * pVkCtx->timestampPeriod * NS_TO_MS
		};
	}

	ht_pipeline_stats ResolvePipelineStatsQuery( const vk_pipestats_query& hQuery ) const
	{
		u32 resultIdx = hQuery.begIdx * pplnStatsQueryPool.queryStrideInSlots;

		return {
			.name						= hQuery.name,
			.inputAssemblyVtxNum		= VkBufferHostView<u64>( pipelineStatsQueryBuff )[ resultIdx + 0 ],
			.inputAssemblyPrimitiveNum	= VkBufferHostView<u64>( pipelineStatsQueryBuff )[ resultIdx + 1 ],
			.vsInvocationNum			= VkBufferHostView<u64>( pipelineStatsQueryBuff )[ resultIdx + 2 ],
			.clipInvocationNum			= VkBufferHostView<u64>( pipelineStatsQueryBuff )[ resultIdx + 3 ],
			.clipPrimitiveNum			= VkBufferHostView<u64>( pipelineStatsQueryBuff )[ resultIdx + 4 ],
			.psInvocationCount			= VkBufferHostView<u64>( pipelineStatsQueryBuff )[ resultIdx + 5 ],
			.csInvocationCount			= VkBufferHostView<u64>( pipelineStatsQueryBuff )[ resultIdx + 6 ]
		};
	}

	vk_timestamp_query BeginTimedZone( vk_command_buffer& cmdBuff, const char* name )
	{
		// NOTE: these are 2 per query so we need std::size * 2
		u16				    currOffset	= ( u16 ) std::size( timedZonesQueries ) * 2;
		vk_timestamp_query  hQuery		= { .name = name, .begIdx = currOffset, .endIdx = currOffset + 1u };
		cmdBuff.CmdWriteTimestamp( timestampQueryPool, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, hQuery.begIdx );
		timedZonesQueries.push_back( hQuery );
		return hQuery;
	}

	void EndTimedZone( vk_command_buffer& cmdBuff, const vk_timestamp_query& hQuery )
	{
		cmdBuff.CmdWriteTimestamp( timestampQueryPool, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, hQuery.endIdx );
	}

	vk_pipestats_query BeginStatsQuery( vk_command_buffer& cmdBuff, const char* name )
	{
		vk_pipestats_query hQuery = {
			.name	= name,
			.begIdx = ( u16 ) std::size( pipelineStatsQueries ),
			.endIdx = u16( -1 )
		};
		cmdBuff.CmdQueryBegin( pplnStatsQueryPool, hQuery.begIdx );
		pipelineStatsQueries.push_back( hQuery );
		return hQuery;
	}

	void EndStatsQuery( vk_command_buffer& cmdBuff, const vk_pipestats_query& hQuery )
	{
		cmdBuff.CmdQueryEnd( pplnStatsQueryPool, hQuery.begIdx );
	}
};

static vk_gpu_frame_profiler HtMakeGpuFrameProfiler( u32 maxQueryCount )
{
    vk_query_pool timestampQueryPool = pVkCtx->CreateQueryPool( maxQueryCount, VK_QUERY_TYPE_TIMESTAMP );
    vk_query_pool pplnStatsQueryPool = pVkCtx->CreateQueryPool( maxQueryCount, VK_QUERY_TYPE_PIPELINE_STATISTICS );

	return {
	    .timestampQueryPool     = timestampQueryPool,
	    .pplnStatsQueryPool     = pplnStatsQueryPool,
	    .timedZonesQueries      = borrowed_array{ ArenaNewArray<vk_timestamp_query>( *pDebugArena, 1000 ) },
	    .pipelineStatsQueries   = borrowed_array{ ArenaNewArray<vk_pipestats_query>( *pDebugArena, 128 ) },
		.timestampQueryBuff     = pVkCtx->CreateBuffer( {
			.name		= "Buff_ReadbackTimestampQueries",
			.usageFlags	= buffer_usage::XFER_DST,
			.szInBytes	= timestampQueryPool.GetSizeInSlots() * sizeof( u64 ),
			.memProps	= buffer_mem_props::HOST_VISIBLE
		} ),
		.pipelineStatsQueryBuff = pVkCtx->CreateBuffer( {
			.name		= "Buff_ReadbackPipelineStatsQueries",
			.usageFlags	= buffer_usage::XFER_DST,
			.szInBytes	= pplnStatsQueryPool.GetSizeInSlots() * sizeof( u64 ),
			.memProps	= buffer_mem_props::HOST_VISIBLE
		} )
	};
}

static u32                                          g_CurrentFifIdx         = 0;
static std::array<vk_gpu_frame_profiler, MAX_FIF>   g_HtGpuFrameProfiler    = {};

HT_FORCEINLINE vk_gpu_frame_profiler *const HtGetGpuFrameProfiler()
{
	return &g_HtGpuFrameProfiler[ g_CurrentFifIdx ];
}

#define HT_TIMED_ZONE( cmdBuff, name, code )										 \
	do																				 \
	{																				 \
		auto hQuery = HtGetGpuFrameProfiler()->BeginTimedZone( cmdBuff, name );		 \
		code;																		 \
		HtGetGpuFrameProfiler()->EndTimedZone( cmdBuff, hQuery );					 \
	} while( 0 )

#define HT_PIPELINE_STATS_QUERY( cmdBuff, name, code )								\
	do																				\
	{																				\
		auto hQuery = HtGetGpuFrameProfiler()->BeginStatsQuery( cmdBuff, name );	\
		code;																		\
		HtGetGpuFrameProfiler()->EndStatsQuery( cmdBuff, hQuery );					\
	} while( 0 )

#include <ht_renderer_types.h>


struct imgui_pass
{
	static_assert( sizeof( ImDrawVert ) == sizeof( imgui_vertex ) );
	static_assert( sizeof( ImDrawIdx ) == sizeof( u16 ) );

    VkPipeline							guiPipeline;

	std::array<vk_buffer, MAX_FIF>		vtx = {};
	std::array<vk_buffer, MAX_FIF>		idx = {};
	vk_image							fontAtlasImg;
	VkSampler							fontSampler;

    desc_hndl32                         dsecFontAtlas;
    desc_hndl32                         descFontSampler;

    void Init( VkFormat colDstFormat )
    {
        scoped_arena tempArena = { pThreadCtx->scratchArenas[ 0 ] };

        vk_shader vtxShader = pVkCtx->CreateShaderFromSpirv(
            SysReadWholeFile( "bin/SpirV/vertex_ImGuiVsMain.spirv", tempArena ) );
        vk_shader fragShader = pVkCtx->CreateShaderFromSpirv(
            SysReadWholeFile( "bin/SpirV/pixel_ImGuiPsMain.spirv", tempArena ) );

        defer {
            pVkCtx->DestroyShaderModule( vtxShader.module );
            pVkCtx->DestroyShaderModule( fragShader.module );
        };

        vk_gfx_pso_config guiState = {
            .polyMode				= VK_POLYGON_MODE_FILL,
            .cullFlags				= VK_CULL_MODE_NONE,
            .frontFace				= VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .primTopology			= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .srcColorBlendFactor	= VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp			= VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor	= VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp			= VK_BLEND_OP_ADD,
            .depthCompareOp			= VK_COMPARE_OP_NEVER,
            .depthWrite				= false,
            .depthTestEnable		= false,
            .blendCol				= true
        };

        vk_gfx_shader_stage shaderStages[] = { vtxShader, fragShader };
        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

        guiPipeline = pVkCtx->CreateGfxPipeline( shaderStages, dynamicStates, std::array{ colDstFormat },
            VK_FORMAT_UNDEFINED, guiState );

        i32 width   = 0;
        i32 height  = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32( nullptr, &width, &height );

        fontAtlasImg = pVkCtx->CreateImage( {
            .name		= "Img_ImGuiFonts",
            .format		= VK_FORMAT_R8G8B8A8_UNORM,
            .type		= VK_IMAGE_TYPE_2D,
            .usgFlags	= image_usage::SAMPLED | image_usage::XFER_DST,
            .width		= ( u16 ) width,
            .height		= ( u16 ) height,
            .layerCount = 1,
            .mipCount	= 1,
        } );
        dsecFontAtlas = pVkCtx->AllocDescriptorIdx( { fontAtlasImg.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

        VkSamplerCreateInfo samplerCreateInfo = {
            .sType						= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter					= VK_FILTER_LINEAR,
            .minFilter					= VK_FILTER_LINEAR,
            .mipmapMode					= VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .maxAnisotropy				= 1.0f,
            .minLod						= 0,
            .maxLod						= VK_LOD_CLAMP_NONE,
            .borderColor				= VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
            .unnormalizedCoordinates	= VK_FALSE,
        };

        fontSampler     = pVkCtx->CreateSampler( samplerCreateInfo );
        descFontSampler = pVkCtx->AllocDescriptorIdx( fontSampler );

        for( vk_buffer& buff : vtx )
        {
            buff = pVkCtx->CreateBuffer( {
                .usageFlags = buffer_usage::STORAGE | buffer_usage::BDA,
                .szInBytes  = 24 * KB,
                .memProps   = buffer_mem_props::HOST_VISIBLE
            } );
        }

        for( vk_buffer& buff : idx )
        {
            buff = pVkCtx->CreateBuffer( {
                .usageFlags = buffer_usage::INDEX,
                .szInBytes  = 4 * KB,
                .memProps   = buffer_mem_props::HOST_VISIBLE
            } );
        }
    }

	void CreateUploadFontAtlasSync( vk_command_buffer& cmdBuff, u64 frameIdx )
	{
		u8* pixels  = nullptr;
		i32 width   = 0;
	    i32 height  = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );

		u64 sizeInBytes = width * height * 4;

		vk_buffer stagingBuff = pVkCtx->CreateBuffer( {
			.usageFlags		= buffer_usage::XFER_SRC,
			.szInBytes	    = sizeInBytes,
			.memProps		= buffer_mem_props::STAGING
		} );

		std::memcpy( stagingBuff.hostVisible, pixels, sizeInBytes );

		cmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_XFER, HT_SYNC_EXEC_MASK_XFER,
			HT_SYNC_CACHE_FLUSH_MASK_XFER, HT_SYNC_CACHE_INVAL_MASK_XFER,
			{ fontAtlasImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL } );
		cmdBuff.CmdCopyBufferToImageSubresource( stagingBuff, 0, fontAtlasImg, VkFullResourceLayers( fontAtlasImg ) );
		cmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_XFER, HT_SYNC_EXEC_MASK_GFX,
			HT_SYNC_CACHE_FLUSH_MASK_XFER, HT_SYNC_CACHE_INVAL_MASK_GFX,
			{ fontAtlasImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			    VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		pVkCtx->PushResourceFree( vk_resc_deletion{ stagingBuff, frameIdx } );
	}

	// NOTE: it's mostly inspired by the official backend code
	void DrawUiPass( vk_command_buffer& cmdBuff, const vk_image& dstTarget, u64	fifIdx )
    {
        vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Draw Imgui Pass", {} );

		const ImDrawData* drawData = ImGui::GetDrawData();

		// NOTE: to avoid rendering when minimized;
		// scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		float2 fbDims = {
		    drawData->DisplaySize.x * drawData->FramebufferScale.x,
            drawData->DisplaySize.y * drawData->FramebufferScale.y
		};
		if( ht::any( fbDims <= 0.0f ) ) return;

		// Textures
		if( drawData->TotalVtxCount <= 0 ) return;

		vk_buffer& refVtxBuff = vtx[ fifIdx ];
		vk_buffer& refIdxBuff = idx[ fifIdx ];

        HT_ASSERT( refVtxBuff.sizeInBytes >= drawData->TotalVtxCount * sizeof( imgui_vertex ) );
        HT_ASSERT( refIdxBuff.sizeInBytes >= drawData->TotalIdxCount * sizeof( u16 ) );

		VkRect2D renderArea = VkGetScissor( dstTarget.width, dstTarget.height );

		VkRenderingAttachmentInfo attInfos[] = {
			VkMakeAttachmentInfo( dstTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} )
		};

		// NOTE: we need a different viewport since this is drawn directly to the screen
		vk_rendering_info renderingInfo = {
			.colorAttachments	= attInfos,
			.viewport			= {
			    0, 0, ( float ) dstTarget.width, ( float ) dstTarget.height, 0, 1.0f
			},
		    // NOTE: this is ignored
			.scissor			= renderArea
		};

		vk_scoped_renderpass renderPass = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

		cmdBuff.CmdBindPipelineAndBindlessDesc( guiPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS );

        float2 scale    = { 2.0f / drawData->DisplaySize.x, 2.0f / drawData->DisplaySize.y };
        float2 move	    = { -1.0f - drawData->DisplayPos.x * scale.x, -1.0f - drawData->DisplayPos.y * scale.y };

        imgui_pass_args pushConst = {
            .scale          = scale,
            .translate      = move,
            .vtxAddr        = refVtxBuff.devicePointer,
            .texAtlasIdx    = dsecFontAtlas.slot,
            .samplerIdx     = descFontSampler.slot
        };

		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );

		// ( 0, 0 ) unless using multi-viewports
		float2 clipOff = { drawData->DisplayPos.x, drawData->DisplayPos.y };
		// ( 1, 1 ) unless using retina display which are often ( 2, 2 )
		float2 clipScale = { drawData->FramebufferScale.x, drawData->FramebufferScale.y };

	    auto vtxDst	= borrowed_array{ VkBufferHostView<ImDrawVert>( refVtxBuff ) };
	    auto idxDst	= borrowed_array{ VkBufferHostView<ImDrawIdx>( refIdxBuff ) };

		for( const ImDrawList* cmdList : drawData->CmdLists )
		{
		    u32 idxBase = ( u32 ) std::size( idxDst );
		    u32 vtxBase = ( u32 ) std::size( vtxDst );
		    vtxDst.append_range( cmdList->VtxBuffer );
		    idxDst.append_range( cmdList->IdxBuffer );

			for( const ImDrawCmd& imCmd : cmdList->CmdBuffer )
			{
			    float4 clipRect = std::bit_cast<float4>( imCmd.ClipRect );

				// Project scissor/clipping rectangles into framebuffer space
				float2 clipMin = ( clipRect.xy - clipOff ) * clipScale;
				float2 clipMax = ( clipRect.zw - clipOff ) * clipScale;

				// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
				clipMin = ht::max( clipMin, float2{ 0.0f, 0.0f } );
				clipMax = ht::min( clipMax, float2{ ( float ) renderArea.extent.width, ( float ) renderArea.extent.height } );

				if( ht::any( clipMax < clipMin ) ) continue;

			    float2 clipDim = clipMax - clipMin;
			    cmdBuff.CmdSetScissor( { i32( clipMin.x ), i32( clipMin.y ),
                    u32( clipDim.x ), u32( clipDim.y ) } );
			    cmdBuff.CmdDrawIndexed( refIdxBuff, VK_INDEX_TYPE_UINT16, imCmd.ElemCount, 1,
			        imCmd.IdxOffset + idxBase, imCmd.VtxOffset + vtxBase, 0 );
			}
		}
	}
};


struct debug_draw_passes
{
	// TODO: where to place these ?
	//static constexpr box_vertices			unitCube	= GenerateDbgBoxFromBounds( BOX_MIN, BOX_MAX );
	//static constexpr box_wireframe_indices	lineVtxBuff = GenerateBoxWireframeIndices();
	//constexpr box_triangle_indices trisVtxBuff = BoxVerticesAsTriangles( unitCube );

	borrowed_array<dbg_aabb_instance>		cpuInstView = {};

	vk_buffer			vtxBuff 			= {};
	vk_buffer			idxBuff 			= {};
	vk_buffer			gpuInstBuff			= {};
	vk_buffer			gpuInstCountBuff	= {};
	vk_buffer			cpuInstBuff			= {};

	vk_buffer			drawCmdsBuff		= {};
	vk_buffer			drawCountBuff 		= {};

	VkPipeline			drawAsLines 		= {};
	VkPipeline			drawAsTriangles		= {};
	// NOTE: it's easier to separate the recording of instances and the submission of draws
	vk_compute_pipeline	compRecordDbgDraw	= {};

	desc_hndl32			gpuInstBuffIdx		= {};
	desc_hndl32			gpuInstCountBuffIdx = {};

	void Init( vk_renderer_config& rndCfg )
	{
	    scoped_arena tempArena = { pThreadCtx->scratchArenas[ 0 ] };

		vk_shader vtx = pVkCtx->CreateShaderFromSpirv(
		    SysReadWholeFile( "bin/SpirV/vertex_DbgBoxVsMain.spirv", tempArena ) );
		vk_shader frag = pVkCtx->CreateShaderFromSpirv(
		    SysReadWholeFile( "bin/SpirV/pixel_ColPassPsMain.spirv", tempArena ) );

		defer {
			pVkCtx->DestroyShaderModule( vtx.module );
			pVkCtx->DestroyShaderModule( frag.module );
		};

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		vk_gfx_shader_stage gfxStages[] = { vtx, frag };

		{
			vk_gfx_pso_config lineDrawPipelineState = {
				.polyMode			= VK_POLYGON_MODE_LINE,
				.cullFlags			= HT_CULL_MODE,
				.frontFace			= HT_FRONT_FACE,
				.primTopology		= VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
				.depthCompareOp		= VK_COMPARE_OP_NEVER,
				.depthWrite			= false,
				.depthTestEnable	= false,
				.blendCol			= false,
			};

			drawAsLines = pVkCtx->CreateGfxPipeline( gfxStages, dynamicStates, std::array{ rndCfg.desiredColorFormat },
			    VK_FORMAT_UNDEFINED, lineDrawPipelineState );
		}

		//vk_gfx_pso_config triDrawPipelineState = {
		//	.polyMode			= VK_POLYGON_MODE_FILL,
		//	.cullFlags			= VK_CULL_MODE_NONE,
		//	.frontFace			= VK_FRONT_FACE_COUNTER_CLOCKWISE,
		//	.primTopology		= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		//	.depthWrite			= VK_TRUE,
		//	.depthTestEnable	= VK_TRUE,
		//	.blendCol			= VK_TRUE
		//};
		//
		//drawAsTriangles = dc.CreateGfxPipeline( gfxStages, dynamicStates, &rndCfg.desiredColorFormat,
		//	1, rndCfg.desiredDepthFormat, triDrawPipelineState );

		vk_shader recordDbgDraw = pVkCtx->CreateShaderFromSpirv(
			SysReadWholeFile( "bin/SpirV/compute_RecordDbgDrawCsMain.spirv", tempArena ) );
		defer {  pVkCtx->DestroyShaderModule( recordDbgDraw.module ); };

		compRecordDbgDraw = pVkCtx->CreateComputePipeline( recordDbgDraw );

		drawCountBuff = pVkCtx->CreateBuffer( {
			.name		= "Buff_DbgDrawCount",
			.usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA | buffer_usage::INDIRECT | buffer_usage::XFER_DST,
			.szInBytes	= 1 * sizeof( u32 ),
		    .memProps	= buffer_mem_props::GPU_ONLY,
		    .alignment  = HT_GPU_CACHELINE_SZ_IN_BYTES
		} );

		drawCmdsBuff = pVkCtx->CreateBuffer( {
			.name		= "Buff_DbgDraws",
			.usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA | buffer_usage::INDIRECT,
			.szInBytes  = MAX_INSTANCES_IN_SCENE * sizeof( draw_instanced_indexed_indirect ),
			.memProps	= buffer_mem_props::GPU_ONLY
		} );

		gpuInstBuff = pVkCtx->CreateBuffer( {
			.name		= "Buff_DbgGpuInst",
			.usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes  = MAX_INSTANCES_IN_SCENE * sizeof( dbg_aabb_instance ),
			.memProps	= buffer_mem_props::GPU_ONLY
		} );
		gpuInstBuffIdx = pVkCtx->AllocDescriptorIdx( gpuInstBuff );

		gpuInstCountBuff = pVkCtx->CreateBuffer( {
			.name		= "Buff_DbgGpuInstCount",
			.usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA | buffer_usage::INDIRECT | buffer_usage::XFER_DST,
			.szInBytes  = 1 * sizeof( u32 ),
		    .memProps	= buffer_mem_props::GPU_ONLY,
		    .alignment  = HT_GPU_CACHELINE_SZ_IN_BYTES
		} );
		gpuInstCountBuffIdx = pVkCtx->AllocDescriptorIdx( gpuInstCountBuff );

		cpuInstBuff = pVkCtx->CreateBuffer( {
			.name		= "Buff_DbgCpuInst",
			.usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	= 10'000 * sizeof( dbg_aabb_instance ),
			.memProps	= buffer_mem_props::HOST_VISIBLE
		} );
		cpuInstView = { VkBufferHostView<dbg_aabb_instance>( cpuInstBuff ) };
	}

	void InitAndUploadDebugGeometry()
	{
		// NOTE: host vis to simplify uploads
		// NOTE: arbitrary sizes chosen
		vtxBuff = pVkCtx->CreateBuffer( {
			.name			= "Buff_DbgVtx",
			.usageFlags		=  buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	= 500 * sizeof( dbg_vertex ),
			.memProps		= buffer_mem_props::HOST_VISIBLE
		} );

		idxBuff = pVkCtx->CreateBuffer( {
			.name			= "Buff_DbgIdx",
			.usageFlags		= buffer_usage::INDEX | buffer_usage::BDA,
			.szInBytes	= 2000 * sizeof( u16 ),
			.memProps		= buffer_mem_props::HOST_VISIBLE
		} );

		//std::memcpy( vtxBuff.hostVisible, std::data( unitCube ), BYTE_COUNT( unitCube ) );
		//std::memcpy( idxBuff.hostVisible, std::data( lineVtxBuff ), BYTE_COUNT( lineVtxBuff ) );
	}

	void ResetDrawCounters( vk_command_buffer& cmdBuff )
	{
		cmdBuff.CmdFillBuffer( drawCountBuff, 0u );
		cmdBuff.CmdFillBuffer( gpuInstCountBuff, 0u );
	}

	void AssembleCPUInstanceBuffer( const float4x4& frustumTransf, bool aabbDraws, bool frustumDraw )
	{
		cpuInstView.resize( 0 );

		[[ unlikely ]]
		if( frustumDraw )
		{
			cpuInstView.push_back( {
				.toWorld	= frustumTransf,
				.color		= Unorm8ToF32( HT_CYAN ),
				//.minAabb	= BOX_MIN,
				//.maxAabb	= BOX_MAX
			} );
		}

		[[ unlikely ]]
		if( aabbDraws )
		{

		}
	}

	void DrawWireframesGPU( vk_command_buffer& cmdBuff, const vk_image& colorTarget, desc_hndl32 camIdx )
    {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Dbg_DrawWireframesGPU", {} );
		{
			record_dbg_draw_params pushBlock = {
				.gpuInstCountAddr	= gpuInstCountBuff.devicePointer,
				.dbgDrawCmdsAddr	= drawCmdsBuff.devicePointer,
				.dbgDrawCountAddr	= drawCountBuff.devicePointer,
				//.indexCount			= ( u32 ) std::size( lineVtxBuff ),
				.firstIndex			= 0,
				.vertexOffset		= 0
			};
			cmdBuff.DispatchCompute( compRecordDbgDraw, pushBlock, u32x3{ 1, 1, 1 } );
		}
		{
			VkRenderingAttachmentInfo attInfos[] = {
				VkMakeAttachmentInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} )
			};

			vk_rendering_info renderingInfo = {
				.colorAttachments	= attInfos,
				.pDepthAttachment	= nullptr,
				.viewport			= VkCorrectedGetViewport( colorTarget.width, colorTarget.height ),
				.scissor			= VkGetScissor( colorTarget.width, colorTarget.height )
			};

			vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

			cmdBuff.CmdBindPipelineAndBindlessDesc( drawAsLines, VK_PIPELINE_BIND_POINT_GRAPHICS );

			dbg_box_params pushBlock = {
				.instBuffAddr	= gpuInstBuff.devicePointer, // NOTE: it's the GPU assembled inst buffer !!!!
				.vtxBuffAddr	= vtxBuff.devicePointer,
				.camIdx			= camIdx.slot
			};
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDrawIndexedIndirectCount<draw_instanced_indexed_indirect>( idxBuff, VK_INDEX_TYPE_UINT16,
				drawCmdsBuff, drawCountBuff );
		}
	}

	// TODO: don't hardcode
	void DbgDrawWireframeCPU( vk_command_buffer& cmdBuff, const vk_image& colorTarget, desc_hndl32 camIdx )
    {
		if( !std::size( cpuInstView ) ) return;

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Dbg_DrawWireframeCPU", {} );

		VkRenderingAttachmentInfo attInfos[] = { VkMakeAttachmentInfo( colorTarget.view,
			VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} )
		};

		vk_rendering_info renderingInfo = {
			.colorAttachments	= attInfos,
			.pDepthAttachment	= nullptr,
			.viewport			= VkCorrectedGetViewport( colorTarget.width, colorTarget.height ),
			.scissor			= VkGetScissor( colorTarget.width, colorTarget.height )
		};

		vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

		cmdBuff.CmdBindPipelineAndBindlessDesc( drawAsLines, VK_PIPELINE_BIND_POINT_GRAPHICS );

		dbg_box_params pushBlock = {
			.instBuffAddr	= cpuInstBuff.devicePointer, // NOTE: it's the CPU assembled buffer !!!!
			.vtxBuffAddr	= vtxBuff.devicePointer,
			.camIdx			= camIdx.slot
		};
		cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
		//cmdBuff.CmdDrawIndexed( idxBuff, VK_INDEX_TYPE_UINT16, ( u32 ) std::size( lineVtxBuff ),
		//	1, 0, 0, 0  );
	}
};


using index_t = u8;

struct culling_pass_args
{
	const vk_buffer&	dbgGpuInstBuff;
	const vk_buffer&	dbgGpuInstCountBuff;

	u32					instCount;

	desc_hndl32			hizDesc;
	desc_hndl32			samplerDesc;

    bool                enableInstCull;
    bool                enableMltCull;
    bool                enableMeshLod;
    bool                enableMltLod;
};

// TODO: dispatch continuation in the passes !
struct culling_pass
{
    // NOTE: these are max capped
	vk_buffer			visibleInstances;
	vk_buffer			visibleInstCounter;
	vk_buffer			meshletsToTestBuff;
	vk_buffer			meshletsToTestCounter;

	vk_buffer			occludedInstances;
	vk_buffer			occludedInstancesCounter;
	vk_buffer			occludedMeshlets;
	vk_buffer			occludedMeshletsCounter;

	vk_buffer			dispatchIndirect;

	vk_buffer			drawCmds;
	vk_buffer			drawData;
	vk_buffer			drawCounter;

	vk_compute_pipeline	instCullPipeline;
	vk_compute_pipeline	instExpansionPipeline;
	vk_compute_pipeline	indirectDispatchPipeline;
	vk_compute_pipeline	meshletCullPipeline;
	vk_compute_pipeline	cullingInitPipeline;

	void Init()
	{
		scoped_arena tempArena = { pThreadCtx->scratchArenas[ 0 ] };

		vk_shader instCull = pVkCtx->CreateShaderFromSpirv( SysReadWholeFile(
			"bin/SpirV/compute_InstCullCsMain.spirv", tempArena ) );
		instCullPipeline = pVkCtx->CreateComputePipeline( instCull );

		vk_shader instExp = pVkCtx->CreateShaderFromSpirv( SysReadWholeFile(
			"bin/SpirV/compute_ExpandDrawsCsMain.spirv", tempArena ) );
		instExpansionPipeline = pVkCtx->CreateComputePipeline( instExp );

		vk_shader dispatcher = pVkCtx->CreateShaderFromSpirv( SysReadWholeFile(
		    "bin/SpirV/compute_IndirectDispatcherCsMain.spirv", tempArena ) );
		indirectDispatchPipeline = pVkCtx->CreateComputePipeline( dispatcher );

		vk_shader meshletCs = pVkCtx->CreateShaderFromSpirv( SysReadWholeFile(
		    "bin/SpirV/compute_MeshletCullCsMain.spirv", tempArena ) );
		meshletCullPipeline = pVkCtx->CreateComputePipeline( meshletCs );

		vk_shader initCs = pVkCtx->CreateShaderFromSpirv( SysReadWholeFile(
		    "bin/SpirV/compute_CsMainCullingInit.spirv", tempArena ) );
		cullingInitPipeline = pVkCtx->CreateComputePipeline( initCs );

		defer {
			pVkCtx->DestroyShaderModule( instCull.module );
			pVkCtx->DestroyShaderModule( instExp.module );
			pVkCtx->DestroyShaderModule( dispatcher.module );
			pVkCtx->DestroyShaderModule( meshletCs.module );
			pVkCtx->DestroyShaderModule( initCs.module );
		};

		drawCounter = pVkCtx->CreateBuffer( {
			.name			= "Buff_DrawCount",
			.usageFlags 	= buffer_usage::STORAGE | buffer_usage::BDA | buffer_usage::INDIRECT,
			.szInBytes 	    = 1 * sizeof( u32 ),
			.memProps		= buffer_mem_props::GPU_ONLY,
		    .alignment      = HT_GPU_CACHELINE_SZ_IN_BYTES
		} );

		drawCmds = pVkCtx->CreateBuffer( {
			.name			= "Buff_DrawCmds",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA | buffer_usage::INDIRECT ,
			.szInBytes	    = MAX_MESHLETS_IN_SCENE * sizeof( draw_meshlet_command ),
			.memProps		= buffer_mem_props::GPU_ONLY
		} );

		drawData = pVkCtx->CreateBuffer( {
			.name			= "Buff_DrawData",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = MAX_MESHLETS_IN_SCENE * sizeof( draw_meshlet_cmd_data ),
			.memProps		= buffer_mem_props::GPU_ONLY
		} );

		dispatchIndirect = pVkCtx->CreateBuffer( {
			.name			= "Buff_DispatchIndirect",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA | buffer_usage::INDIRECT,
			.szInBytes	    = 1 * sizeof( dispatch_command ),
			.memProps		= buffer_mem_props::GPU_ONLY
		} );

		visibleInstances = pVkCtx->CreateBuffer( {
			.name			= "Buff_VisibleInstances",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = MAX_INSTANCES_IN_SCENE * sizeof( visible_instance ),
			.memProps		= buffer_mem_props::GPU_ONLY
		} );

		visibleInstCounter = pVkCtx->CreateBuffer( {
			.name			= "Buff_VisibleInstCounter",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = 1 * sizeof( u32 ),
			.memProps		= buffer_mem_props::GPU_ONLY,
		    .alignment      = HT_GPU_CACHELINE_SZ_IN_BYTES
		} );

		occludedInstances = pVkCtx->CreateBuffer( {
			.name			= "Buff_OccludedInstances",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = MAX_INSTANCES_IN_SCENE * sizeof( u32 ),
			.memProps		= buffer_mem_props::GPU_ONLY
		} );

		occludedInstancesCounter = pVkCtx->CreateBuffer( {
			.name			= "Buff_OccludedInstCounter",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = 1 * sizeof( u32 ),
		    .memProps		= buffer_mem_props::GPU_ONLY,
		    .alignment      = HT_GPU_CACHELINE_SZ_IN_BYTES
		} );

		meshletsToTestBuff = pVkCtx->CreateBuffer( {
			.name			= "Buff_MeshletsToProcess",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = MAX_MESHLETS_IN_SCENE * sizeof( meshlet_cull_wok_item ),
			.memProps		= buffer_mem_props::GPU_ONLY
		} );

		meshletsToTestCounter = pVkCtx->CreateBuffer( {
			.name			= "Buff_MeshletsToProcessCount",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = 1 * sizeof( u32 ),
		    .memProps		= buffer_mem_props::GPU_ONLY,
		    .alignment      = HT_GPU_CACHELINE_SZ_IN_BYTES
		} );

		occludedMeshlets = pVkCtx->CreateBuffer( {
			.name			= "Buff_OccludedMeshlets",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = MAX_MESHLETS_IN_SCENE * sizeof( meshlet_cull_wok_item ),
			.memProps		= buffer_mem_props::GPU_ONLY
		} );

		occludedMeshletsCounter = pVkCtx->CreateBuffer( {
			.name			= "Buff_OccludedMeshletsCount",
			.usageFlags		= buffer_usage::STORAGE | buffer_usage::BDA,
			.szInBytes	    = 1 * sizeof( u32 ),
		    .memProps		= buffer_mem_props::GPU_ONLY,
		    .alignment      = HT_GPU_CACHELINE_SZ_IN_BYTES
		} );
	}

	void Execute( vk_command_buffer& cmdBuff, const culling_pass_args& args, bool latePass )
    {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Cull Pass",{} );

		constexpr VkMemoryBarrier2 computeBarrier = VkMakeBarrier(
		    HT_SYNC_EXEC_MASK_COMP_SRC, HT_SYNC_CACHE_FLUSH_MASK_SHADER,
		    HT_SYNC_EXEC_MASK_COMP_DST, HT_SYNC_CACHE_INVAL_MASK_SHADER );

	    culling_pass_params pushBlock = {
	        .visibleInstAddr            = visibleInstances.devicePointer,
	        .visibleInstCounterAddr     = visibleInstCounter.devicePointer,
	        .occludedInstAddr           = occludedInstances.devicePointer,
	        .occludedInstCounterAddr    = occludedInstancesCounter.devicePointer,

	        .mltToTestAddr              = !latePass ? meshletsToTestBuff.devicePointer : occludedMeshlets.devicePointer,
	        .mltToTestCounterAddr       = !latePass ? meshletsToTestCounter.devicePointer : occludedMeshletsCounter.devicePointer,
	        .occludedMltAddr            = !latePass ? occludedMeshlets.devicePointer : 0,
	        .occludedMltCounterAddr     = !latePass ? occludedMeshletsCounter.devicePointer : 0,

	        .dispatchCmdsAddr           = dispatchIndirect.devicePointer,
	        .drawCmdsAddr               = drawCmds.devicePointer,
	        .drawDataAddr               = drawData.devicePointer,
	        .drawCounterAddr            = drawCounter.devicePointer,

	        .hizTexIdx                  = args.hizDesc.slot,
	        .hizSamplerIdx              = args.samplerDesc.slot,
	        .instCount                  = args.instCount,

	        .isLatePass                 = latePass,
	        .instCullWorkGrX            = instCullPipeline.groupSize.x,

	        .enableInstCull             = args.enableInstCull,
	        .enableMltCull              = args.enableMltCull,
	        .enableMeshLod              = args.enableMeshLod,
	        .enableMltLod               = args.enableMltLod
	    };

	    cmdBuff.DispatchCompute( cullingInitPipeline, pushBlock, u32x3{ 1, 1, 1 } );

		cmdBuff.CmdPipelineMemoryBarriers( std::array{ computeBarrier } );

	    fixed_string<64> instCullingRegionName = { "Instance Culling {}", latePass ? 1 : 0 };
	    HT_PIPELINE_STATS_QUERY( cmdBuff, ( const char* ) instCullingRegionName, cmdBuff.DispatchComputeIndirect(
            instCullPipeline, pushBlock, dispatchIndirect ) );

		cmdBuff.CmdPipelineMemoryBarriers( std::array{ computeBarrier } );

	    {
		    indirect_dispatcher_params pushBlockDispatch = {
		        .dispatchCmdBuffAddr    = dispatchIndirect.devicePointer,
		        .counterBufferAddr      = visibleInstCounter.devicePointer,
		        .cullShaderWorkGrX      = instExpansionPipeline.groupSize.x
		    };
		    cmdBuff.DispatchCompute( indirectDispatchPipeline, pushBlockDispatch, u32x3{ 1, 1, 1 } );
	    }

		cmdBuff.CmdPipelineMemoryBarriers( std::array{ computeBarrier } );

	    cmdBuff.DispatchComputeIndirect( instExpansionPipeline, pushBlock, dispatchIndirect );

		cmdBuff.CmdPipelineMemoryBarriers( std::array{ computeBarrier } );

	    {
		    indirect_dispatcher_params pushBlockDispatch = {
		        .dispatchCmdBuffAddr    = dispatchIndirect.devicePointer,
		        .counterBufferAddr      = !latePass ?
		                                    meshletsToTestCounter.devicePointer : occludedMeshletsCounter.devicePointer,
		        .cullShaderWorkGrX      = meshletCullPipeline.groupSize.x
		    };
		    cmdBuff.DispatchCompute( indirectDispatchPipeline, pushBlockDispatch, u32x3{ 1, 1, 1 } );
	    }

		cmdBuff.CmdPipelineMemoryBarriers( std::array{ computeBarrier } );

	    fixed_string<64> mltCullRegionName = { "Meshlet Culling {}", latePass ? 1 : 0 };
	    HT_PIPELINE_STATS_QUERY( cmdBuff, ( const char* ) mltCullRegionName, cmdBuff.DispatchComputeIndirect(
            meshletCullPipeline, pushBlock, dispatchIndirect ) );
	}
};

struct tone_mapping_pass
{
	vk_buffer			averageLuminanceBuffer;
	vk_buffer			luminanceHistogramBuffer;
	vk_buffer			atomicWgCounterBuff;

	vk_compute_pipeline	compAvgLumPipe;
	vk_compute_pipeline	compTonemapPipe;

	desc_hndl32			avgLumIdx;
	desc_hndl32			atomicWgCounterIdx;
	desc_hndl32			lumHistoIdx;

	void Init()
	{
		scoped_arena tempArena = { pThreadCtx->scratchArenas[ 0 ] };

		vk_shader avgLum = pVkCtx->CreateShaderFromSpirv(
			SysReadWholeFile( "bin/SpirV/compute_AvgLuminanceCsMain.spirv", tempArena ) );
		compAvgLumPipe = pVkCtx->CreateComputePipeline( avgLum );

		vk_shader toneMapper = pVkCtx->CreateShaderFromSpirv(
			SysReadWholeFile( "bin/SpirV/compute_TonemappingGammaCsMain.spirv", tempArena ) );
		compTonemapPipe = pVkCtx->CreateComputePipeline( toneMapper );

		defer {
			pVkCtx->DestroyShaderModule( avgLum.module );
			pVkCtx->DestroyShaderModule( toneMapper.module );
		};

		averageLuminanceBuffer = pVkCtx->CreateBuffer( {
			.name = "Buff_AvgLum",
			.usageFlags = buffer_usage::STORAGE | buffer_usage::XFER_DST,
			.szInBytes = 1 * sizeof( float ),
			.memProps = buffer_mem_props::GPU_ONLY
		} );
		avgLumIdx = pVkCtx->AllocDescriptorIdx( averageLuminanceBuffer );

		atomicWgCounterBuff = pVkCtx->CreateBuffer( {
			.name = "Buff_TonemappingAtomicWgCounter",
			.usageFlags = buffer_usage::STORAGE | buffer_usage::XFER_DST,
			.szInBytes = 1 * sizeof( u32 ),
			.memProps = buffer_mem_props::GPU_ONLY
		} );
		atomicWgCounterIdx = pVkCtx->AllocDescriptorIdx( atomicWgCounterBuff );

		luminanceHistogramBuffer = pVkCtx->CreateBuffer( {
			.name = "Buff_LumHisto",
			.usageFlags = buffer_usage::STORAGE | buffer_usage::XFER_DST,
			.szInBytes = 4 * sizeof( u64 ),
			.memProps = buffer_mem_props::GPU_ONLY } );
		lumHistoIdx = pVkCtx->AllocDescriptorIdx( luminanceHistogramBuffer );
	}

	void AverageLuminancePass(
		vk_command_buffer&		cmdBuff,
		const vk_image&			colTarget,
		desc_hndl32				hdrColSrcDesc,
		float					dt
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Average Lum Pass", {} );

		//cmdBuff.CmdFillBuffer( luminanceHistogramBuffer, 0u );
		//cmdBuff.CmdFillBuffer( atomicWgCounterBuff, 0u );

		//rscTracker.UseBuffer( luminanceHistogramBuffer, HT_COMPUTE_READWRITE );
		//rscTracker.UseBuffer( atomicWgCounterBuff, HT_COMPUTE_READWRITE );
		//rscTracker.UseImage( colTarget, HT_COMPUTE_READ, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
		//rscTracker.FlushBarriers( cmdBuff );

		// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
		avg_luminance_info avgLumInfo = {
			.minLogLum = -10.0f,
			.invLogLumRange = 1.0f / 12.0f,
			.dt = dt
		};

		struct push_const
		{
			avg_luminance_info  avgLumInfo;
			u32					hdrColSrcIdx;
			u32					lumHistoIdx;
			u32					atomicWorkGrCounterIdx;
			u32					avgLumIdx;
		} pushConst = { avgLumInfo, hdrColSrcDesc.slot, lumHistoIdx.slot, atomicWgCounterIdx.slot, avgLumIdx.slot };

		cmdBuff.DispatchCompute( compAvgLumPipe, pushConst,
			GroupCount( u32x3{ colTarget.width, colTarget.height, 1 }, compAvgLumPipe.groupSize ) );
	}

	void TonemappingGammaPass(
		vk_command_buffer&		cmdBuff,
		const vk_image&			dstImg,
		desc_hndl32				hdrColDesc,
		desc_hndl32				sdrColDesc,
		u32x2					hdrTrgSize
	) {
		HT_ASSERT( ( hdrTrgSize.x == dstImg.width ) && ( hdrTrgSize.y == dstImg.height ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Tonemapping Gamma Pass", {} );

		struct push_const
		{
			u32 hdrColIdx;
			u32 sdrColIdx;
			u32 avgLumIdx;
		} pushConst = { hdrColDesc.slot, sdrColDesc.slot, avgLumIdx.slot };

		cmdBuff.DispatchCompute( compTonemapPipe, pushConst,
			GroupCount( u32x3{ hdrTrgSize.x, hdrTrgSize.y, 1 }, compTonemapPipe.groupSize ) );
	}
};

struct depth_pyramid_pass
{
	vk_compute_pipeline		pow2DownsamplerPipeline;
	vk_compute_pipeline		multiPassPipeline;
	vk_image				hzb; // NOTE: we'll only transition to general once
	VkImageView				hzbMipViews[ MAX_MIP_LEVELS ];

	vk_buffer				atomicWgCounterBuff;

	VkSampler       		quadMinSampler;
	VkSampler       		pointSampler;

	desc_hndl32     		hzbDesc;
	desc_hndl32				hzbMipsDesc[ MAX_MIP_LEVELS ];

	desc_hndl32				atomicWgCounterIdx;

	desc_hndl32				quadMinSamplerIdx;

	desc_hndl32				pointSamplerIdx;

	void Init( u16 srcWidth, u16 srcHeight )
	{
		scoped_arena tempArena = { pThreadCtx->scratchArenas[ 0 ] };

		vk_shader downsampler = pVkCtx->CreateShaderFromSpirv(
			SysReadWholeFile( "bin/SpirV/compute_Pow2DownSamplerCsMain.spirv", tempArena ) );
		defer { pVkCtx->DestroyShaderModule( downsampler.module ); };

		multiPassPipeline = pVkCtx->CreateComputePipeline( downsampler );

		vk_shader pow2DownsamplerShader = pVkCtx->CreateShaderFromSpirv(
			SysReadWholeFile( "bin/SpirV/compute_DownsamplerCsMain.spirv", tempArena ) );
		defer { pVkCtx->DestroyShaderModule( pow2DownsamplerShader.module ); };

		pow2DownsamplerPipeline = pVkCtx->CreateComputePipeline( pow2DownsamplerShader );

		u16 hzbWidth    = std::bit_floor( srcWidth );
		u16 hzbHeight   = std::bit_floor( srcHeight );

		image_info hzbInfo = {
			.name		= "Img_HZB",
			.format		= VK_FORMAT_R32_SFLOAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= image_usage::SAMPLED | image_usage::STORAGE | image_usage::XFER_SRC | image_usage::XFER_DST,
			.width		= hzbWidth,
			.height		= hzbHeight,
			.layerCount = 1,
			.mipCount	= ( u8 ) GetImgMipCount( hzbWidth, hzbHeight )
		};

		hzb = pVkCtx->CreateImage( hzbInfo );

		hzbDesc = pVkCtx->AllocDescriptorIdx( { hzb.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		for( u32 mi = 0; mi < hzb.mipCount; ++mi )
		{
			hzbMipViews[ mi ] = VkMakeImgView( pVkCtx->device, hzb.hndl, hzbInfo.format, mi, 1,
				VK_IMAGE_VIEW_TYPE_2D, 0, hzbInfo.layerCount );
			hzbMipsDesc[ mi ] = pVkCtx->AllocDescriptorIdx( { hzbMipViews[ mi ], VK_IMAGE_LAYOUT_GENERAL } );
		}

		atomicWgCounterBuff = pVkCtx->CreateBuffer( {
			.name       = "Buff_DownsamplerAtomicWgCounter",
			.usageFlags = buffer_usage::STORAGE | buffer_usage::XFER_DST,
			.szInBytes  = 1 * sizeof( u32 ),
		    .memProps   = buffer_mem_props::GPU_ONLY,
		    .alignment  = HT_GPU_CACHELINE_SZ_IN_BYTES
		} );
		atomicWgCounterIdx = pVkCtx->AllocDescriptorIdx( atomicWgCounterBuff );


		VkSamplerReductionModeCreateInfo reduxInfo = {
			.sType			= VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
			.reductionMode	= VK_SAMPLER_REDUCTION_MODE_MIN,
		};

		VkSamplerCreateInfo reduxSamplerCreateInfo = {
			.sType						= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.pNext						= &reduxInfo,
			.magFilter					= VK_FILTER_LINEAR,
			.minFilter					= VK_FILTER_LINEAR,
			.mipmapMode					= VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.maxAnisotropy				= 1.0f,
			.minLod 					= 0,
			.maxLod						= VK_LOD_CLAMP_NONE,
			.borderColor				= VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates	= VK_FALSE,
		};

		quadMinSampler      = pVkCtx->CreateSampler( reduxSamplerCreateInfo );
		quadMinSamplerIdx   = pVkCtx->AllocDescriptorIdx( { quadMinSampler } );

		VkSamplerCreateInfo pointSamplerCreateInfo = {
			.sType						= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter					= VK_FILTER_NEAREST,
			.minFilter					= VK_FILTER_NEAREST,
			.mipmapMode					= VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW				= VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.maxAnisotropy				= 1.0f,
			.minLod 					= 0,
			.maxLod						= VK_LOD_CLAMP_NONE,
			.borderColor				= VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates	= VK_FALSE,
		};
		pointSampler    = pVkCtx->CreateSampler( pointSamplerCreateInfo );
		pointSamplerIdx = pVkCtx->AllocDescriptorIdx( { pointSampler } );
	}

	void Execute( vk_command_buffer& cmdBuff, const vk_image& depthTarget, desc_hndl32 depthIdx )
    {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "HZB Single Pass", {} );

		downsampler_params pushConst = {
			.srcResolution			= { depthTarget.width, depthTarget.height },
			.mip0Resolution			= { hzb.width, hzb.height },
			.mipCount				= hzb.mipCount,
			.reductionSamplerIdx	= quadMinSamplerIdx.slot,
			.srcSrvIdx				= depthIdx.slot,
			.dstMipsIdx				= {},
			.atomicWgCounterIdx		= atomicWgCounterIdx.slot,
			.isMip0FromNonPot		= u32( !IsPowOf2( depthTarget.width ) || !IsPowOf2( depthTarget.height ) ),
		};

		HT_ASSERT( hzb.mipCount <= std::size( pushConst.dstMipsIdx ) );
		for( u32 mi = 0; mi < std::size( pushConst.dstMipsIdx ); ++mi )
		{
			pushConst.dstMipsIdx[ mi ] = ( mi < hzb.mipCount ) ? hzbMipsDesc[ mi ].slot : ~0u;
		}

		u32x3 dispatchSize = {
			GroupCount( hzb.width, MIP0_TILE_SIZE.x ),
			GroupCount( hzb.height, MIP0_TILE_SIZE.y ),
			1
		};
		cmdBuff.DispatchCompute( pow2DownsamplerPipeline, pushConst, dispatchSize );
	}

	void ExecuteMultiPass( vk_command_buffer& cmdBuff, const vk_image& depthTarget, desc_hndl32	depthIdx )
    {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "HZB Multi Pass", {} );

		VkMemoryBarrier2 executionBarrier[] = { {
				.sType			= VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask	= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			    .dstStageMask	= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		} };

		u32 mipLevel    = 0;
		u32 srcImg      = depthIdx.slot;
		u32 srcWidth    = depthTarget.width;
		u32 srcHeight   = depthTarget.height;
		for( u32 mi = 0; mi < hzb.mipCount; ++mi )
		{
			[[unlikely]]
			if( mi > 0 )
			{
				mipLevel = mi - 1;
				srcImg = hzbDesc.slot;
			}

			u32 levelWidth  = std::max( 1u, u32( hzb.width ) >> mi );
			u32 levelHeight = std::max( 1u, u32( hzb.height ) >> mi );

			multi_pass_downsampler_params pushConst = {
				.srcSize				= { srcWidth, srcHeight },
				.dstSize				= { levelWidth, levelHeight },
				.reductionSamplerIdx	= quadMinSamplerIdx.slot,
				.pointSamplerIdx		= pointSamplerIdx.slot,
				.inImgIdx				= srcImg,
				.inImgLod				= mipLevel,
				.outImgIdx				= hzbMipsDesc[ mi ].slot,
				.isMip0FromNonPot		= u32( 0 == mi )
			};

			cmdBuff.DispatchCompute( multiPassPipeline, pushConst,
				GroupCount( u32x3{ levelWidth, levelHeight, 1 }, multiPassPipeline.groupSize ) );
			cmdBuff.CmdPipelineMemoryBarriers( executionBarrier );

			srcWidth    = levelWidth;
			srcHeight   = levelHeight;
		}
	}
};

struct vbuffer_pass_args
{
	const vk_image&			depthTarget;

	const vk_buffer&      	indexBuff;
	VkIndexType           	indexType;

	const vk_buffer&		drawCmds;
	const vk_buffer&		drawData;
	const vk_buffer&		drawCount;
};

struct vbuffer_pass
{
	static constexpr VkFormat	VBUFF_FORMAT = VK_FORMAT_R32G32_UINT;

	vk_image				vbuffRG32Target;

	VkPipeline				gfxVBuffPipeline;
	vk_compute_pipeline	    compDbgHashTriToScPipeline;
	vk_compute_pipeline	    compLambertianClayPipeline;

	desc_hndl32				vbuffRG32Srv;

	void Init( VkFormat depthFormat, u16 width, u16 height )
	{
		vbuffRG32Target = pVkCtx->CreateImage( {
			.name		= "Img_VBufferTarget",
			.format		= VBUFF_FORMAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= image_usage::COLOR_TARGET | image_usage::SAMPLED,
			.width		= width,
			.height		= height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		vbuffRG32Srv = pVkCtx->AllocDescriptorIdx( { vbuffRG32Target.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		scoped_arena tempArena = { pThreadCtx->scratchArenas[ 0 ] };

		vk_shader vtx = pVkCtx->CreateShaderFromSpirv(
		    SysReadWholeFile( "bin/SpirV/vertex_VBufferVsMain.spirv", tempArena ) );
		vk_shader frag = pVkCtx->CreateShaderFromSpirv(
		    SysReadWholeFile( "bin/SpirV/pixel_VBufferPsMain.spirv", tempArena ) );

		defer {
			pVkCtx->DestroyShaderModule( vtx.module );
			pVkCtx->DestroyShaderModule( frag.module );
		};

		vk_gfx_pso_config vbuffState = {
			.polyMode			= VK_POLYGON_MODE_FILL,
			.cullFlags			= HT_CULL_MODE,
			.frontFace			= HT_FRONT_FACE,
			.primTopology		= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.depthCompareOp		= VK_COMPARE_OP_GREATER,
			.depthWrite			= true,
			.depthTestEnable	= true,
			.blendCol			= false
		};

		vk_gfx_shader_stage shaderStages[] = { vtx, frag };
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		gfxVBuffPipeline = pVkCtx->CreateGfxPipeline( shaderStages, dynamicStates, std::array{ VBUFF_FORMAT },
		    depthFormat, vbuffState, pVkCtx->globalPipelineLayout );

		vk_shader comp = pVkCtx->CreateShaderFromSpirv(
				SysReadWholeFile( "bin/SpirV/compute_VBufferDbgDrawCsMain.spirv", tempArena ) );
		compDbgHashTriToScPipeline = pVkCtx->CreateComputePipeline( comp );
		vk_shader lambert = pVkCtx->CreateShaderFromSpirv(
				SysReadWholeFile( "bin/SpirV/compute_LambertianClayCsMain.spirv", tempArena ) );
		compLambertianClayPipeline = pVkCtx->CreateComputePipeline( lambert );

		defer {
			pVkCtx->DestroyShaderModule( comp.module );
			pVkCtx->DestroyShaderModule( lambert.module );
		};
	}

	void DrawIndexedIndirect( vk_command_buffer& cmdBuff, const vbuffer_pass_args& args, bool latePass )
    {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "VBuffer Pass", {} );

		// NOTE: since we do this incrementally we want the 2nd pass to just load
		const VkAttachmentLoadOp loadOp = latePass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;

		VkClearValue clear = { .color = GetVBufferClearValue() };

		VkRenderingAttachmentInfo attInfos[] = {
			VkMakeAttachmentInfo( vbuffRG32Target.view, loadOp, VK_ATTACHMENT_STORE_OP_STORE, clear )
		};
		VkRenderingAttachmentInfo depthWrite = VkMakeAttachmentInfo( args.depthTarget.view, loadOp,
			VK_ATTACHMENT_STORE_OP_STORE, { .depthStencil = REV_Z_DEPTH_BUFFER_CLEAR_VAL } );

		vk_rendering_info renderingInfo = {
			.colorAttachments	= attInfos,
			.pDepthAttachment	= &depthWrite,
			.viewport			= VkCorrectedGetViewport( vbuffRG32Target.width, vbuffRG32Target.height ),
			.scissor			= VkGetScissor( vbuffRG32Target.width, vbuffRG32Target.height )
		};

		vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

		cmdBuff.CmdBindPipelineAndBindlessDesc( gfxVBuffPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS );

		vbuffer_params pushBlock = {
			.drawDataAddr   = args.drawData.devicePointer,
		};
		cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
		cmdBuff.CmdDrawIndexedIndirectCount<draw_meshlet_command>( args.indexBuff, args.indexType,
			args.drawCmds, args.drawCount );
	}

	void DebugDrawHashedVBuffer( vk_command_buffer&	cmdBuff, const vk_image& dstImg, desc_hndl32 dstImgIdx )
    {
		HT_ASSERT( ( dstImg.width == vbuffRG32Target.width ) && ( dstImg.height == vbuffRG32Target.height ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "VBuffer Dbg Tri hash Pass", {} );

		vbuffer_dbg_draw_params pushBlock = {
			.vbuffRes	= { vbuffRG32Target.width, vbuffRG32Target.height },
			.srcIdx		= vbuffRG32Srv.slot,
			.dstIdx		= dstImgIdx.slot
		};
		cmdBuff.DispatchCompute( compDbgHashTriToScPipeline, pushBlock,
			GroupCount( u32x3{ vbuffRG32Target.width, vbuffRG32Target.height, 1 },
				compDbgHashTriToScPipeline.groupSize ) );
	}

	void DbgDrawAsLamberitanClay( vk_command_buffer& cmdBuff, const vk_image& dstImg, desc_hndl32 dstWriteDesc )
    {
		HT_ASSERT( ( vbuffRG32Target.width == dstImg.width ) && ( vbuffRG32Target.height == dstImg.height ) );

		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Dbg_LambertianClayPass", {} );

		lambertian_clay_params pushBlock = {
			.vbuffRes		= { dstImg.width, dstImg.height },
			.vbuffIdx		= vbuffRG32Srv.slot,
			.dstIdx			= dstWriteDesc.slot
		};
		cmdBuff.DispatchCompute( compLambertianClayPipeline, pushBlock,
			GroupCount( u32x3{ dstImg.width, dstImg.height, 1 }, compLambertianClayPipeline.groupSize ) );
	}
};

struct fwd_pass_args
{
	const vk_image&		colorTarget;
	const vk_image&		depthTarget;

	const vk_buffer&	indexBuff;
	VkIndexType     	indexType;

	const vk_buffer&	drawCmds;
	const vk_buffer&	drawData;
	const vk_buffer&	drawCount;
};

struct fwd_pass
{
	VkPipeline 			gfxDepthPrepass;
	VkPipeline 			gfxLambertianClay;

	void Init( VkFormat depthFormat, VkFormat colorFormat )
	{
		scoped_arena tempArena = { pThreadCtx->scratchArenas[ 0 ] };

		vk_shader vtxDepth = pVkCtx->CreateShaderFromSpirv( SysReadWholeFile(
				"bin/SpirV/vertex_DepthPrepassVsMain.spirv", tempArena ) );

		vk_gfx_pso_config depthPrepassState = {
			.polyMode			= VK_POLYGON_MODE_FILL,
			.cullFlags			= HT_CULL_MODE,
			.frontFace			= HT_FRONT_FACE,
			.primTopology		= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.depthCompareOp		= VK_COMPARE_OP_GREATER,
			.depthWrite			= true,
			.depthTestEnable	= true,
			.blendCol			= false
		};

		vk_gfx_shader_stage shaderStagesDepth[] = { vtxDepth };
		VkDynamicState dynamicStatesDepth[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		gfxDepthPrepass = pVkCtx->CreateGfxPipeline( shaderStagesDepth, dynamicStatesDepth, {},
		    depthFormat, depthPrepassState, pVkCtx->globalPipelineLayout );

		vk_shader vtx = pVkCtx->CreateShaderFromSpirv( SysReadWholeFile(
				"bin/SpirV/vertex_MeshletPassVsMain.spirv", tempArena ) );
		vk_shader frag = pVkCtx->CreateShaderFromSpirv( SysReadWholeFile(
			"bin/SpirV/pixel_MeshletClayPassPsMain.spirv", tempArena ) );

		vk_gfx_pso_config gfxState = {
			.polyMode				= VK_POLYGON_MODE_FILL,
			.cullFlags				= HT_CULL_MODE,
			.frontFace				= HT_FRONT_FACE,
			.primTopology			= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.srcColorBlendFactor	= VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp			= VK_BLEND_OP_ADD,
			.depthCompareOp			= VK_COMPARE_OP_GREATER_OR_EQUAL,
			.depthWrite				= false,
			.depthTestEnable		= true
		};

		vk_gfx_shader_stage shaderStages[] = { vtx, frag };
		VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			//VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
			//VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
			VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT
		};

		gfxLambertianClay = pVkCtx->CreateGfxPipeline( shaderStages, dynamicStates, std::array{ colorFormat },
		    depthFormat, gfxState, pVkCtx->globalPipelineLayout );

		defer {
			pVkCtx->DestroyShaderModule( vtxDepth.module );
			pVkCtx->DestroyShaderModule( vtx.module );
			pVkCtx->DestroyShaderModule( frag.module );
		};
	}

	void DrawIndexedIndirect(
		vk_command_buffer&		cmdBuff,
		const fwd_pass_args&	args,
		bool					latePass,
		bool					isXRayOn
	) {
		vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Depth_Prepass + Fwd/XRay Pass", {} );

		// NOTE: since we do this incrementally we want the 2nd pass to just load
		const VkAttachmentLoadOp loadOp = latePass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;

		{
			VkRenderingAttachmentInfo depthWrite = VkMakeAttachmentInfo( args.depthTarget.view, loadOp,
				VK_ATTACHMENT_STORE_OP_STORE, { .depthStencil = REV_Z_DEPTH_BUFFER_CLEAR_VAL } );

			vk_rendering_info renderingInfo = {
				.pDepthAttachment	= &depthWrite,
				.viewport			= VkCorrectedGetViewport( args.colorTarget.width, args.colorTarget.height ),
				.scissor			= VkGetScissor( args.colorTarget.width, args.colorTarget.height )
			};

			vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo );

			cmdBuff.CmdBindPipelineAndBindlessDesc( gfxDepthPrepass, VK_PIPELINE_BIND_POINT_GRAPHICS );

			depth_prepass_params pushBlock = {
				.drawDataAddr	= args.drawData.devicePointer
			};
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDrawIndexedIndirectCount<draw_meshlet_command>( args.indexBuff, args.indexType,
				args.drawCmds, args.drawCount );
		}
		ht_img_layout_transition depthToTest[] = {
			{ args.depthTarget, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL }
		};
		cmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_GFX, HT_SYNC_EXEC_MASK_GFX,
			HT_SYNC_CACHE_FLUSH_MASK_DEPTH_TARGET, HT_SYNC_CACHE_INVAL_MASK_DEPTH_TARGET, depthToTest );
		{
			VkRenderingAttachmentInfo attInfos[] = {
				VkMakeAttachmentInfo( args.colorTarget.view, loadOp, VK_ATTACHMENT_STORE_OP_STORE, {} )
			};
			VkRenderingAttachmentInfo depthRead = VkMakeAttachmentInfo( args.depthTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD,
				VK_ATTACHMENT_STORE_OP_NONE, {} );

			vk_rendering_info renderingInfo = {
				.colorAttachments	= attInfos,
				.pDepthAttachment	= &depthRead,
				.viewport			= VkCorrectedGetViewport( args.colorTarget.width, args.colorTarget.height ),
				.scissor			= VkGetScissor( args.colorTarget.width, args.colorTarget.height )
			};

			vk_gfx_dynamic_state dynamicState = {
				.colorBlendDynamicStateOn	= true,
				.colorBlendEnabled			= isXRayOn
			};

			vk_scoped_renderpass dynamicRendering = cmdBuff.CmdIssueScopedRenderPass( renderingInfo, &dynamicState );

			cmdBuff.CmdBindPipelineAndBindlessDesc( gfxLambertianClay, VK_PIPELINE_BIND_POINT_GRAPHICS );

			meshlet_pass_params pushBlock = {
				.drawDataAddr	= args.drawData.devicePointer
			};
			cmdBuff.CmdPushConstants( &pushBlock, sizeof( pushBlock ) );
			cmdBuff.CmdDrawIndexedIndirectCount<draw_meshlet_command>( args.indexBuff, args.indexType,
				args.drawCmds, args.drawCount );
		}
	}
};

struct ht_lod_alloc
{
    offset_alloc_t meshlets;
    offset_alloc_t payload;
};

struct ht_mesh_alloc
{
    ht_lod_alloc    hLodAllocs[ MAX_LOD_LEVELS_COUNT ] = {};
    u32             meshID      = -1;
    u32             refCount    = 0;
};

struct virtual_frame
{
	VkSemaphore                 canGetImgSema;

	vk_buffer	                viewData;
	vk_buffer                   gpuMeshRefs; // NOTE: this does the gpuMeshRefs[ node.meshId ] -----> meshDesc
    vk_buffer                   gpuMeshTable;
	vk_buffer                   gpuInstances;

	desc_hndl32                 viewDataIdx;
    desc_hndl32					gpuMeshTableDesc;
	desc_hndl32                 instDesc;
};

static virtual_frame MakeVirtualFrame(
    u64 viewsCount,
    u64 maxMeshes,
    u64 maxLiveMeshes,
    u64 instCount,
    u32 fifIdx
) {
	fixed_string<64> name = { "Buff_VirtualFrame_ViewBuff{}", fifIdx };

	vk_buffer viewData = pVkCtx->CreateBuffer( {
		.name		= &name[ 0 ],
		.usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA,
		.szInBytes	= viewsCount * sizeof( view_data ),
		.memProps	= buffer_mem_props::HOST_VISIBLE
	} );

    fixed_string<64> meshRefsName = { "Buff_VirtualFrame_MeshRefs{}", fifIdx };

	vk_buffer gpuMeshRefs = pVkCtx->CreateBuffer( {
		.name		= &meshRefsName[ 0 ],
		.usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA,
		.szInBytes	= maxMeshes * sizeof( u32 ), // NOTE: should be plenty for any work size
		.memProps	= buffer_mem_props::HOST_VISIBLE
	} );

    fixed_string<64> meshTableName = { "Buff_VirtualFrame_MeshTable{}", fifIdx };

    vk_buffer gpuMeshTable = pVkCtx->CreateBuffer( {
        .name		= &meshTableName[ 0 ],
        .usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA,
        .szInBytes	= maxLiveMeshes * sizeof( gpu_mesh ),
        .memProps	= buffer_mem_props::HOST_VISIBLE
    } );

	fixed_string<64> instName = { "Buff_VirtualFrame_Instances{}", fifIdx };

	vk_buffer gpuInstances = pVkCtx->CreateBuffer( {
		.name		= &instName[ 0 ],
		.usageFlags	= buffer_usage::STORAGE | buffer_usage::BDA,
		.szInBytes	= instCount * sizeof( gpu_instance ),
		.memProps	= buffer_mem_props::HOST_VISIBLE
	} );

	return {
		.canGetImgSema		= pVkCtx->CreateBinarySemaphore(),
		.viewData			= viewData,
	    .gpuMeshRefs        = gpuMeshRefs,
	    .gpuMeshTable       = gpuMeshTable,
		.gpuInstances		= gpuInstances,

		.viewDataIdx		= pVkCtx->AllocDescriptorIdx( viewData ),
	    .gpuMeshTableDesc   = pVkCtx->AllocDescriptorIdx( gpuMeshTable ),
		.instDesc		    = pVkCtx->AllocDescriptorIdx( gpuInstances )
	};
}

// NOTE: we always create a gpu_mesh descriptor before upload
// the actual payload is uploaded on the transfer queue and the engine polls the associated fence,
// when it triggers all the new associated instances will be promoted;
// So basically the gpu_mesh buffer can point to stall or unresolved data,
// but the engine only issues the available instances and only valid data is accessed.
struct renderer_context final : renderer_interface
{
	alignas( 8 ) vk_renderer_config                 config = {};

	culling_pass							        cullingPass;
	imgui_pass								        imguiPass;
	depth_pyramid_pass						        hzbPass;
	tone_mapping_pass						        tonemappingPass;
	debug_draw_passes						        dbgPass;
	vbuffer_pass							        vBuffPass;
	fwd_pass								        fwdPass;

	soa_slot_array<ht_mesh_alloc, gpu_mesh>         meshTable;
    using HMESH32 = decltype( meshTable )::hndl_t;

    std::span<HMESH32>                              cpuMeshRefs;

	std::array<virtual_frame, MAX_FIF>	            vrtFrames;

	vk_buffer                                       stagingBuff;

    vk_buffer                                       megaGpuPayloadBuff;
	vk_buffer                                       megaGpuMeshletBuff;

    offset_allocator_t						        meshletAllocator;
    offset_allocator_t						        payloadAllocator;

	vk_buffer								        globalData;

	vk_image								        depthTarget;
	vk_image								        colorTarget;

	u64										        vFrameIdx = 0;

	VkSampler								        pbrSampler;
	desc_hndl32								        pbrSamplerIdx;

	desc_hndl32								        globalDataIdx;

	desc_hndl32								        depthSrv;
	desc_hndl32								        colorDesc;

	const u32								        framesInFlight = MAX_FIF;


	void            InitBackend( u64 hInst, u64 hWnd ) override;

	bool            PollJobCompletion( atomic_u64* hJobDoneSignal ) override
	{
	    // TODO: don't hardcode like this; but we know the upload flow here sooooo
	    // NOTE: device-device deps maybe ?
	    // https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#synchronization-semaphores-waiting
	    // search: "The second access scope includes all memory access performed by the device"
	    return VkGetTimelineSemaValue( pVkCtx->device, pVkCtx->copyQueue.timelineSema ) >=
	        SysAtomicRead64<sys_fence_t::ACQ>( hJobDoneSignal );
	}
	void            UploadMeshes( atomic_u64* hJobDoneSignal, std::span<const mesh_upload_req> meshAssets,
	                                void* diskAssetStore, linear_arena& arena ) override;

	void            HostFrames( const frame_data& frameData, linear_arena& scratchArena, gpu_data& gpuData ) override;

    std::span<u8>   GetDmaBuffer() override { return VkBufferHostView<u8>( stagingBuff ); }

	u32             UpdateSceneData( virtual_frame& thisVFrame, const frame_data& frameData )
    {
	    std::ranges::copy( frameData.views, ( view_data* ) thisVFrame.viewData.hostVisible );
	    std::ranges::copy( meshTable.get<gpu_mesh>(), ( gpu_mesh* ) thisVFrame.gpuMeshTable.hostVisible );
	    std::ranges::copy( cpuMeshRefs, ( HMESH32* ) thisVFrame.gpuMeshRefs.hostVisible );
	    std::ranges::copy( frameData.instances, ( gpu_instance* ) thisVFrame.gpuInstances.hostVisible );

	    return 0;// /* numValidInstances */ ( u32 ) std::size( frameData.instances );
    }

	void            CreateGlobalTargets( u16 width, u16 height )
	{
		depthTarget = pVkCtx->CreateImage( {
			.name		= "Img_DepthTarget",
			.format		= config.DEPTH_FORMAT,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= image_usage::DEPTH_TARGET | image_usage::SAMPLED,
			.width		= width,
			.height		= height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		depthSrv = pVkCtx->AllocDescriptorIdx( { depthTarget.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		colorTarget = pVkCtx->CreateImage( {
			.name		= "Img_ColorTarget",
			.format		= config.desiredColorFormat,
			.type		= VK_IMAGE_TYPE_2D,
			.usgFlags	= image_usage::COLOR_TARGET | image_usage::SAMPLED | image_usage::STORAGE | image_usage::XFER_SRC,
			.width		= width,
			.height		= height,
			.layerCount = 1,
			.mipCount	= 1,
		} );

		colorDesc = pVkCtx->AllocDescriptorIdx( { colorTarget.view, VK_IMAGE_LAYOUT_GENERAL } );
	}
};

renderer_interface* MakeRenderer( linear_arena& arena ) { return ArenaNew<renderer_context>( arena ); }

void renderer_context::InitBackend( u64 hInst, u64 hWnd )
{
	config = { .renderWidth = SCREEN_WIDTH, .renderHeight = SCREEN_HEIGHT };

	pVkCtx = ArenaMake<vk_context>( *pPersistentArena, VkMakeContext( hInst, hWnd, config ) );

	globalData = pVkCtx->CreateBuffer( {
		.usageFlags	= buffer_usage::STORAGE,
		.szInBytes	= MAX_FIF * sizeof( global_data ),
		.memProps	= buffer_mem_props::HOST_VISIBLE
	} );

	cullingPass.Init();
	tonemappingPass.Init();
	dbgPass.Init( config );
	fwdPass.Init( config.DEPTH_FORMAT, config.desiredColorFormat );
	imguiPass.Init( pVkCtx->scConfig.format );

    meshTable = decltype( meshTable ){ *pGameArena, MAX_MESHES_IN_SCENE };
    static_assert( HMESH32::MAX_IDX > MAX_MESHES_IN_SCENE );

    // NOTE: this serves as a CPU dupe for our mesh refs;  u32 idxOfMeshDesc = meshRefs[ node.meshID ];
    cpuMeshRefs = ArenaNewArray<HMESH32>( *pGameArena, MAX_MESHES_CAP );
    std::ranges::fill( cpuMeshRefs, HMESH32{} );


	stagingBuff = pVkCtx->CreateBuffer( {
		.name		    = "StagingBuff",
		.usageFlags	    = buffer_usage::XFER_SRC,
		.szInBytes	    = 256 * MB,
		.memProps	    = buffer_mem_props::STAGING,
	    .alignment      = RAW_DISK_SECTOR_ALIGNMENT // NOTE: this is necessary if we use this as the io ring dst too
	} );

    megaGpuPayloadBuff = pVkCtx->CreateBuffer( {
        .name			= "MegaGpuPayloadBuff",
        .usageFlags		= buffer_usage::STORAGE | buffer_usage::XFER_DST | buffer_usage::BDA | buffer_usage::INDEX,
        .sharingMode    = VK_SHARING_MODE_CONCURRENT,
        .szInBytes	    = MAX_PAYLOAD_SZ_IN_BYTES,
        .memProps		= buffer_mem_props::GPU_ONLY,
    } );

	megaGpuMeshletBuff = pVkCtx->CreateBuffer( {
		.name			= "MegaGpuMeshletBuff",
		.usageFlags		= buffer_usage::STORAGE | buffer_usage::XFER_DST | buffer_usage::BDA,
	    .sharingMode    = VK_SHARING_MODE_CONCURRENT,
		.szInBytes	    = MAX_MESHLETS_IN_SCENE * sizeof( gpu_meshlet ),
		.memProps		= buffer_mem_props::GPU_ONLY,
	} );

    meshletAllocator = offset_allocator_t{ ( u32 ) megaGpuMeshletBuff.sizeInBytes };
    payloadAllocator = offset_allocator_t{ ( u32 ) megaGpuPayloadBuff.sizeInBytes };

	// TODO: move
	VkSamplerCreateInfo samplerCreateInfo = {
		.sType						= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter					= VK_FILTER_LINEAR,
		.minFilter					= VK_FILTER_LINEAR,
		.mipmapMode					= VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW				= VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxAnisotropy				= 1.0f,
		.minLod						= 0,
		.maxLod						= VK_LOD_CLAMP_NONE,
		.borderColor				= VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates	= VK_FALSE,
	};

	pbrSampler		= pVkCtx->CreateSampler( samplerCreateInfo );
	pbrSamplerIdx	= pVkCtx->AllocDescriptorIdx( { pbrSampler } );

	for( u32 fifIdx = 0; fifIdx < MAX_FIF; ++fifIdx )
	{
		vrtFrames[ fifIdx ] = MakeVirtualFrame( 4, MAX_MESHES_CAP,
		    MAX_MESHES_IN_SCENE, MAX_INSTANCES_IN_SCENE, fifIdx );
		g_HtGpuFrameProfiler[ fifIdx ] = HtMakeGpuFrameProfiler( config.maxVkQueryCount );
	}
}

// NOTE: we are keeping all baked stuff relative so now, when uploading, we assign new offsets at the lod level
void renderer_context::UploadMeshes(
	atomic_u64*					        hJobDoneSignal,
	std::span<const mesh_upload_req>	meshUploadReqs,
	void*                               diskAssetStore,
	linear_arena&						arena
) {
	auto    tempArena       = scoped_arena{ arena };

	auto    stagingArray    = borrowed_array{ FromBytes<ht_disk_sector>( GetDmaBuffer() ) };

    u64     uploadsCount    = MAX_LOD_LEVELS_COUNT * std::size( meshUploadReqs );
	auto    mltRegionCopies = borrowed_array{ ArenaNewArray<VkBufferCopy2>( tempArena, uploadsCount ) };
    auto    pldRegionCopies = borrowed_array{ ArenaNewArray<VkBufferCopy2>( tempArena, uploadsCount ) };

	auto LmbdCopyScaffolding = [ & ] ( u64 offset, u64 szBytes, u64 dstOffset ) -> VkBufferCopy2
    {
	    constexpr u64 SEC_ALIGNMENT = alignof( decltype( stagingArray )::value_type );

	    u64 secCount = ( offset % SEC_ALIGNMENT + szBytes + SEC_ALIGNMENT - 1 ) / SEC_ALIGNMENT;
	    u64 firstSec = stagingArray.grow_by( secCount );

	    std::span<u8> srcMltSpan = SysFileRawReadNBytesBlocking( diskAssetStore, offset, szBytes,
	        AsBytesWritable( std::span{ &stagingArray[ firstSec ], secCount } ) );

		return MakeVkBufferCopy2( &srcMltSpan[ 0 ] - ( u8* ) &stagingArray[ 0 ], dstOffset, szBytes );
	};

	for( const auto&[ hpkMesh, meshID ] : meshUploadReqs )
	{
	    if( HMESH32{} != cpuMeshRefs[ meshID ] )
	    {
	        auto& refHtMeshAlloc = meshTable.get<ht_mesh_alloc>( cpuMeshRefs[ meshID ] );
	        refHtMeshAlloc.refCount++;
	        continue;
	    }

	    ht_lod_alloc lodAllocs[ MAX_LOD_LEVELS_COUNT ] = {};
	    for( const auto[ hpkLod, hLodAlloc ] : std::views::zip( hpkMesh.lods, lodAllocs ) )
	    {
	        if( 0 == hpkLod.mltCount ) break; // NOTE: in this scheme 0 means end-of LOD chain

	        hLodAlloc = {
	            .meshlets   = meshletAllocator.Alloc( ( u32 ) ( hpkLod.mltCount * sizeof( gpu_meshlet ) ) ),
                .payload    = payloadAllocator.Alloc( ( u32 ) hpkLod.payloadSize )
            };

	        // TODO: move in the offset allocator and type it ?
	        HT_ASSERT( !( hLodAlloc.meshlets.offset % sizeof( gpu_meshlet ) ) );

	        mltRegionCopies.push_back( LmbdCopyScaffolding( hpkLod.firstMltOffestInBytes,
                hpkLod.mltCount * sizeof( gpu_meshlet ), hLodAlloc.meshlets.offset ) );
	        pldRegionCopies.push_back( LmbdCopyScaffolding( hpkLod.payloadBeginInBytes,
	            hpkLod.payloadSize, hLodAlloc.payload.offset ) );
	    }

	    ht_mesh_alloc meshAlloc = {
	        .hLodAllocs = { lodAllocs[ 0 ], lodAllocs[ 1 ], lodAllocs[ 2 ], lodAllocs[ 3 ] },
            .refCount   = 1
        };

	    gpu_mesh meshWLODs = {
	        .lod4Err    = hpkMesh.lodErrs,
            .aabbMin    = hpkMesh.aabbMin,
            .aabbMax    = hpkMesh.aabbMax,
            .lods       = {
                gpu_mesh_lod{
                    .payloadStartInBytes    = meshAlloc.hLodAllocs[ 0 ].payload.offset,
                    .firstMlt               = meshAlloc.hLodAllocs[ 0 ].meshlets.offset / ( u32 ) sizeof( gpu_meshlet ),
                    .mltCount               = ( u32 ) hpkMesh.lods[ 0 ].mltCount
                },
                gpu_mesh_lod{
                    .payloadStartInBytes    = meshAlloc.hLodAllocs[ 1 ].payload.offset,
                    .firstMlt               = meshAlloc.hLodAllocs[ 1 ].meshlets.offset / ( u32 ) sizeof( gpu_meshlet ),
                    .mltCount               = ( u32 ) hpkMesh.lods[ 1 ].mltCount
                },
                gpu_mesh_lod{
                    .payloadStartInBytes    = meshAlloc.hLodAllocs[ 2 ].payload.offset,
                    .firstMlt               = meshAlloc.hLodAllocs[ 2 ].meshlets.offset / ( u32 ) sizeof( gpu_meshlet ),
                    .mltCount               = ( u32 ) hpkMesh.lods[ 2 ].mltCount
                },
                gpu_mesh_lod{
                    .payloadStartInBytes    = meshAlloc.hLodAllocs[ 3 ].payload.offset,
                    .firstMlt               = meshAlloc.hLodAllocs[ 3 ].meshlets.offset / ( u32 ) sizeof( gpu_meshlet ),
                    .mltCount               = ( u32 ) hpkMesh.lods[ 3 ].mltCount
                }
            }
	    };

	    cpuMeshRefs[ meshID ] = meshTable.push_entry( meshAlloc, meshWLODs );
	}

	vk_command_buffer copyCmdBuff = pVkCtx->AllocateCmdBufferForQueue( vk_queue_t::COPY );

	copyCmdBuff.CmdBeginCmdBuffer();

	copyCmdBuff.CmdCopyBuffer( stagingBuff, megaGpuMeshletBuff, mltRegionCopies );
	copyCmdBuff.CmdCopyBuffer( stagingBuff, megaGpuPayloadBuff, pldRegionCopies );

	copyCmdBuff.CmdEndCmdBuffer();

	SysAtomicWrite64<sys_fence_t::REL>( hJobDoneSignal, pVkCtx->QueueSubmit( pVkCtx->copyQueue, copyCmdBuff ) );
}

void renderer_context::HostFrames( const frame_data& frameData, linear_arena& scratchArena, gpu_data& gpuData )
{
	scoped_arena tempArena      = { scratchArena };

	const u64 currentFrameIdx	= vFrameIdx++;
	const u32 currentFIFIdx	    = currentFrameIdx % framesInFlight;

	g_CurrentFifIdx             = currentFIFIdx;

    HT_ASSERT( VK_TIMEOUT > pVkCtx->TimelineTryWaitFor( pVkCtx->gpuFrameTimeline, framesInFlight, ~0ull )  );

	HtGetGpuFrameProfiler()->ReadbackQueries( gpuData.timedZones, gpuData.pipelinesStats );

	pVkCtx->FlushDeletionQueues( currentFrameIdx );

	virtual_frame& thisVFrame = vrtFrames[ currentFIFIdx ];

	u32 instCount = UpdateSceneData( thisVFrame, frameData );

	vk_command_buffer thisCmdBuff = pVkCtx->AllocateCmdBufferForQueue( vk_queue_t::GFX );

	thisCmdBuff.CmdBeginCmdBuffer();

	HtGetGpuFrameProfiler()->GPUReadAndResetQueryPools( thisCmdBuff );

	vk_timestamp_query hQueryGPUFrame = HtGetGpuFrameProfiler()->BeginTimedZone( thisCmdBuff, "GPU FrameTime" );

	static bool initResources = false;
	if( !initResources )
	{
		pVkCtx->CreateSwapchain();

		CreateGlobalTargets( config.renderWidth, config.renderHeight );

		vBuffPass.Init( depthTarget.format, config.renderWidth, config.renderHeight );
		hzbPass.Init( depthTarget.width, depthTarget.height );

		// TODO: add UploadDataSync function in the renderer
		imguiPass.CreateUploadFontAtlasSync( thisCmdBuff, currentFrameIdx );
		dbgPass.InitAndUploadDebugGeometry();

	    thisCmdBuff.CmdFillBuffer( hzbPass.atomicWgCounterBuff, 0 );

		ht_img_layout_transition initTargets[] = {
			{ depthTarget, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL },
			{ colorTarget, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL },
			{ vBuffPass.vbuffRG32Target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL },
			{ hzbPass.hzb, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL }
		};
		thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_XFER, HT_SYNC_EXEC_MASK_COMP_DST,
			HT_SYNC_CACHE_FLUSH_MASK_XFER, HT_SYNC_CACHE_INVAL_MASK_SHADER, initTargets );

		initResources = true;
	}

	pVkCtx->FlushPendingDescriptorUpdates();

    global_data& refGlobalData = ( ( global_data* ) globalData.hostVisible )[ currentFIFIdx ];
    refGlobalData = {
        .mltAddr		= megaGpuMeshletBuff.devicePointer,
        .pldAddr		= megaGpuPayloadBuff.devicePointer,
        .meshDescAddr   = thisVFrame.gpuMeshTable.devicePointer,
        .meshRefAddr    = thisVFrame.gpuMeshRefs.devicePointer,
        .instAddr       = thisVFrame.gpuInstances.devicePointer,
        .viewsAddr      = thisVFrame.viewData.devicePointer,
        //.lodRequestsAddr =
        .mainCamIdx     = !frameData.dbgDrawFlags.freezeMainView ? 0u : 1u, // TODO: don't hardcode here ?
    };
    thisCmdBuff.CmdPushGlobalData( globalData, currentFIFIdx * sizeof( refGlobalData ), sizeof( refGlobalData ) );

	const culling_pass_args cullPassArgs = {
		.dbgGpuInstBuff			= dbgPass.gpuInstBuff,
		.dbgGpuInstCountBuff	= dbgPass.gpuInstCountBuff,

		.instCount				= instCount,

		.hizDesc				= hzbPass.hzbDesc,
		.samplerDesc			= hzbPass.quadMinSamplerIdx,

	    .enableInstCull         = frameData.dbgDrawFlags.toggleInstCull,
        .enableMltCull	        = frameData.dbgDrawFlags.toggleMltCull,
        .enableMeshLod	        = frameData.dbgDrawFlags.toggleMeshLOD,
        .enableMltLod	        = frameData.dbgDrawFlags.toggleMltLOD
	};

	HT_TIMED_ZONE( thisCmdBuff, "GPU Culling 1nd Pass", cullingPass.Execute( thisCmdBuff, cullPassArgs, false ) );

	const vbuffer_pass_args vbuffPassArgs = {
		.depthTarget	= depthTarget,
		.indexBuff 		= megaGpuPayloadBuff,
		.indexType 		= VK_INDEX_TYPE_UINT8,
		.drawCmds		= cullingPass.drawCmds,
		.drawData		= cullingPass.drawData,
		.drawCount		= cullingPass.drawCounter
	};

    ht_img_layout_transition writeTransitions[] = {
        { depthTarget, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
        { vBuffPass.vbuffRG32Target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL }
    };
	thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_COMP_SRC, HT_SYNC_EXEC_MASK_GFX,
		HT_SYNC_CACHE_FLUSH_MASK_SHADER, HT_SYNC_CACHE_INVAL_MASK_GFX, writeTransitions );
	vBuffPass.DrawIndexedIndirect( thisCmdBuff, vbuffPassArgs, false );

	const fwd_pass_args fwdPassArgs = {
		.colorTarget	= colorTarget,
		.depthTarget	= depthTarget,
		.indexBuff 		= megaGpuPayloadBuff,
		.indexType 		= VK_INDEX_TYPE_UINT8,
		.drawCmds		= cullingPass.drawCmds,
		.drawData		= cullingPass.drawData,
		.drawCount		= cullingPass.drawCounter
	};
	//fwdPass.DrawIndexedIndirect( thisFrameCmdBuff, fwdPassArgs, false, frameData.dbgDrawFlags.drawXRayMode );

    ht_img_layout_transition hzbReadDepth = {
        depthTarget, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL
    };
	thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_GFX, HT_SYNC_EXEC_MASK_COMP_DST,
		HT_SYNC_CACHE_FLUSH_MASK_DEPTH_TARGET, HT_SYNC_CACHE_INVAL_MASK_SHADER, hzbReadDepth );
	hzbPass.Execute( thisCmdBuff, depthTarget, depthSrv );

	HT_TIMED_ZONE( thisCmdBuff, "GPU Culling 2nd Pass", cullingPass.Execute( thisCmdBuff, cullPassArgs, true ) );

	//fwdPass.DrawIndexedIndirect( thisFrameCmdBuff, fwdPassArgs, true, frameData.dbgDrawFlags.drawXRayMode );
	ht_img_layout_transition depthToDrawLate = {
		depthTarget, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
	};
	thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_COMP_SRC, HT_SYNC_EXEC_MASK_GFX,
		HT_SYNC_CACHE_FLUSH_MASK_SHADER, HT_SYNC_CACHE_INVAL_MASK_GFX, depthToDrawLate );
	vBuffPass.DrawIndexedIndirect( thisCmdBuff, vbuffPassArgs, true );

	ht_img_layout_transition targetsToRead[] = {
		{ depthTarget, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL },
		{ vBuffPass.vbuffRG32Target, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL }
	};
	thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_GFX, HT_SYNC_EXEC_MASK_COMP_DST,
		HT_SYNC_CACHE_FLUSH_MASK_DEPTH_TARGET, HT_SYNC_CACHE_INVAL_MASK_SHADER, targetsToRead );
	hzbPass.Execute( thisCmdBuff, depthTarget, depthSrv );

	[[unlikely]]
	if( !frameData.dbgDrawFlags.vBuffPixelHash )
	{
		vBuffPass.DbgDrawAsLamberitanClay( thisCmdBuff, colorTarget, colorDesc );

		// TODO: tonemapping
	}
	else
	{
		vBuffPass.DebugDrawHashedVBuffer( thisCmdBuff, colorTarget, colorDesc );
	}

	// TODO: upload the actual aabbs
	// NOTE: these are conditional
	dbgPass.AssembleCPUInstanceBuffer( frameData.frustTransf, frameData.dbgDrawFlags.dbgDraw,
		frameData.dbgDrawFlags.freezeMainView );
	ht_img_layout_transition colorToDraw = {
		colorTarget, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
	};
	thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_COMP_SRC, HT_SYNC_EXEC_MASK_GFX,
		HT_SYNC_CACHE_FLUSH_MASK_SHADER, HT_SYNC_CACHE_INVAL_MASK_GFX, colorToDraw );
	dbgPass.DbgDrawWireframeCPU( thisCmdBuff, colorTarget, thisVFrame.viewDataIdx );

	thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_GFX, HT_SYNC_EXEC_MASK_GFX,
		HT_SYNC_CACHE_FLUSH_MASK_COL_TARGET, HT_SYNC_CACHE_INVAL_MASK_COL_TARGET );

	imguiPass.DrawUiPass( thisCmdBuff, colorTarget, currentFIFIdx );

	// NOTE: swapchain
	u32 scImgIdx = pVkCtx->AcquireNextSwapchainImageBlocking( thisVFrame.canGetImgSema );
	const vk_swapchain_image& scImg = pVkCtx->scImgs[ scImgIdx ];

	ht_img_layout_transition toCopy[] = {
		{ scImg.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL },
		{ colorTarget, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL }
	};
	thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_ALL, HT_SYNC_EXEC_MASK_XFER,
		HT_SYNC_CACHE_FLUSH_MASK_COL_TARGET, HT_SYNC_CACHE_INVAL_MASK_XFER, toCopy );

	thisCmdBuff.CmdCopyImageSameProps( colorTarget, scImg.img );

	ht_img_layout_transition toPresent[] = {
		{ scImg.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
		{ colorTarget, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL }
	};
	thisCmdBuff.CmdBarrier( HT_SYNC_EXEC_MASK_XFER, HT_SYNC_EXEC_MASK_ALL,
		HT_SYNC_CACHE_FLUSH_MASK_XFER, HT_SYNC_CACHE_INVAL_MASK_SHADER, toPresent );

	HtGetGpuFrameProfiler()->EndTimedZone( thisCmdBuff, hQueryGPUFrame );

	thisCmdBuff.CmdEndCmdBuffer();

	pVkCtx->QueueSubmit( pVkCtx->gfxQueue, thisCmdBuff,
	    std::array{ VkGetSemaSubmit( thisVFrame.canGetImgSema, HT_SYNC_EXEC_MASK_XFER ) },
	    std::array{ VkGetSemaSubmit( scImg.canPresentSema, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT ),
	        pVkCtx->gpuFrameTimeline.GetSignalNextPoint( VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT ) } );

	pVkCtx->QueuePresent( pVkCtx->gfxQueue, scImgIdx );
}
