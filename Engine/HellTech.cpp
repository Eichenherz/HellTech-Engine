#include <ht_core_types.h>
#include <ht_memory.h>
#include <ht_array.h>
#include <ht_math.h>
#include <ht_renderer_types.h>

#include <System/sys_file.h>
#include <System/sys_sync.h>
#include <ht_atomic_stack.h>

#include "engine_platform_api.h"
#include "engine_types.h"
#include "im_gui.h"


#include <System/sys_io_ring.h>

#include <ankerl/unordered_dense.h>


//==================CONSTEXPR===================//
constexpr float YAW_SIGN   = FSignOf( ht::dot( ht::cross( WORLD_UP,    WORLD_FWD ), -WORLD_LEFT ) );
constexpr float PITCH_SIGN = FSignOf( ht::dot( ht::cross( -WORLD_LEFT, WORLD_FWD ), -WORLD_UP ) );
//==============================================//

//===================GLOBALS====================//
static linear_arena g_GameArena     = {};
static linear_arena g_DebugArena    = {};
linear_arena*       pGameArena      = nullptr;
linear_arena*       pDebugArena     = nullptr;
//==============================================//

// Virtual camera
using PFN_LookAtCoord = float4x4 (*) ( float3 eyePos, float3 focusPos, float3 upDir );

struct virtual_camera
{
	static constexpr float3 CAM_FWD = { 0.0f, 0.0f, 1.0f };
	static constexpr float3 CAM_UP	= { 0.0f, 1.0f, 0.0f };

	float4x4			proj		= {};
	float4x4			view		= {};
	float4x4			prevView	= {};
	float3				worldPos	= { 0.0f, 0.0f, 0.0f }; // NOTE: we always start at the world origin ( if centered )
	float3				camViewDir	= {};
	float2				viewportDim	= {};
	PFN_LookAtCoord	    PfnLookAt	= nullptr;
	float				zNear		= NAN;
	// NOTE: pitch must be in [ -pi/2, pi/2 ]
	float				pitch		= 0.0f;
	float				yaw			= 0.0f;

    virtual_camera() = default;

    virtual_camera( float2 viewportDim, float radsYFov, float zNear, bool isRH ) :
        // NOTE: MUST use () here bc of  clang's float4x4 !!!
        proj( PerspRevZInfFarFromFovAndAspectRatio( radsYFov, viewportDim.x / viewportDim.y, zNear, isRH ) ),
        viewportDim{ viewportDim }, PfnLookAt{ isRH ? MatLookAtRH : MatLookAtLH }, zNear{ zNear }
    {}

	void Move( float3 camMove, float2 dRot )
	{
		yaw     = ht::mod_angle( yaw + dRot.x );
		pitch   = std::clamp( pitch + dRot.y, -HT_ALMOST_HALF_PI, HT_ALMOST_HALF_PI );

		float3x3   rot         = RotMatFromPitchYawRoll( pitch, yaw, 0 );
		float3     newWorldPos = worldPos + ht::mul( ht::normalize( camMove ), rot );
		float3     camLookAt   = ht::mul( WORLD_FWD, rot );

		prevView    = view;
		view        = PfnLookAt( newWorldPos, newWorldPos + camLookAt, WORLD_UP );
		worldPos    = newWorldPos;
		camViewDir  = -camLookAt;
	}
};

inline view_data ViewData( const virtual_camera& virtualCam )
{
    return {
        .proj			= virtualCam.proj,
        .mainView		= virtualCam.view,
        .prevView		= virtualCam.prevView,
        .mainViewProj	= virtualCam.view * virtualCam.proj,
        .prevViewProj	= virtualCam.prevView * virtualCam.proj,
        .worldPos		= ( pkd_fp32x3 ) virtualCam.worldPos,
        .zNear			= virtualCam.zNear,
        // NOTE: this must not be negative for LH coords
        .camViewDir		= ( pkd_fp32x3 ) virtualCam.camViewDir,
        .lodTarget		= ( 2.0f / virtualCam.proj[ 1 ][ 1 ] ) * ( 1.0f / float( virtualCam.viewportDim.y ) )
    };
}

// Input
#include <System/Win32/win32_kbd_scancodes.h>

// TODO: don't hardcode
struct ht_demo_action_map
{
	u16 fwd;
	u16 bwd;
	u16 left;
	u16 right;
	u16 up;
	u16 down;
	u16 slowDown;
	u16 frustumDbg;
	u16 xrayDraw;
	u16 instCull;
	u16 mltCull;
	u16 toggleMeshLOD;
	u16 toggleMltLOD;
};

constexpr ht_demo_action_map GLOB_ACTION_MAP = {
	.fwd			= HT_SC_W,
	.bwd			= HT_SC_S,
	.left			= HT_SC_A,
	.right			= HT_SC_D,
	.up				= HT_SC_SPACE,
	.down			= HT_SC_C,
	.slowDown		= HT_SC_LCTRL,
	.frustumDbg 	= HT_SC_F,
	.xrayDraw		= HT_SC_X,
	.instCull		= HT_SC_I,
	.mltCull		= HT_SC_M,
	.toggleMeshLOD	= HT_SC_L,
	.toggleMltLOD	= HT_SC_K
};

struct move_cam_action
{
	float3 camMove;
	float2 dRot;
};

inline move_cam_action GetMoveCamAction(
	const ht_input_state&	inputState,
	float					elapsedTime,
	float					moveSpeed,
	float					mouseSensitivity
) {
	float3 camMove = {};
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.fwd ) )    camMove += WORLD_FWD;
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.left ) )   camMove += WORLD_LEFT;
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.bwd ) )    camMove += -WORLD_FWD;
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.right ) )  camMove += -WORLD_LEFT;
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.up ) )     camMove += WORLD_UP;
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.down ) )   camMove += -WORLD_UP;

	float mvSpeed = moveSpeed;
	if( inputState.IsButtonHeld( GLOB_ACTION_MAP.slowDown ) )
	{
		mvSpeed *= 0.4f;
	}

	if( ht::any( float3{} != camMove ) )
	{
		camMove = ht::normalize( camMove ) * ( mvSpeed * elapsedTime );
	}

	float2 yawPitch = YAW_SIGN * mouseSensitivity * ( float2 ) inputState.dPosMouse;
	return { .camMove = camMove, .dRot = yawPitch };
}

// Job system
job_system_ctx::job_system_ctx()
    : queue{ ArenaNewArray<ht_job_t>( *pPersistentArena, 128 ) } {}
void job_system_ctx::SubmitJob( ht_job_t job )
{
	HT_ASSERT( queue.TryPush( job ) );
	SysSemaphoreRelease( sema, 1 );
}

// Uploads
struct upload_job_payload
{
	borrowed_array<mesh_upload_req>	meshUploads         = {};
	std::span<const gpu_instance>   entitiesToPromote   = {};
	atomic_u64					    hUploadDoneSignal   = ~0ull; // NOTE: bc we check it against the timeline sema
    u64                             allocSzInBytes      = 0; // NOTE: we need this bc we are responsible for the alloc handle
};

static upload_job_payload* HtMakeUploadPayload( u64 maxMeshCap, std::span<const gpu_instance> nodesToPromote )
{
    constexpr u64 headerSzInBytes = sizeof( upload_job_payload );

    u64 meshSzInBytes       = maxMeshCap * sizeof( mesh_upload_req );
    u64 requestSzInBytes    = std::max( BLOCK_SZ_IN_BYTES, headerSzInBytes + meshSzInBytes );

    std::span<u8> mem       = g_pVirtualAllocator->AllocVirtualBlock( requestSzInBytes, HtCurrentThreadIdx() );
    std::span<u8> meshMem   = mem.subspan( headerSzInBytes, meshSzInBytes );

    upload_job_payload* pPayload = ( upload_job_payload* ) &mem[ 0 ];
    *pPayload = {
        .meshUploads        = borrowed_array{ FromBytes<mesh_upload_req>( meshMem ) },
        .entitiesToPromote  = nodesToPromote,
        .allocSzInBytes     = std::size( mem )
    };

    return pPayload;
}

// Engine
struct helltech final : helltech_interface
{
    virtual_camera                      mainActiveCam   = {};
    virtual_camera                      debugCam        = {};

	im_gui_ctx							imGuiCtx		= {};

	renderer_dbg_draw					rndDbgFlags		= {};
	renderer_interface*                 pRenderer		= {};
    // NOTE: these are hard capped, we don't care to grow free the mem OS will do it for us on program exit
    borrowed_array<instance_desc>		drawables		= {};
	borrowed_array<upload_job_payload*> jobCache		= {};

	borrowed_array<ht_timed_zone>		timedZones		= {};
	borrowed_array<ht_pipeline_stats>	pipelinesStats	= {};

    std::span<hpk_sector_desc>          sectorsSpan     = {};
    std::span<gpu_instance>             gpuInstSpan     = {};
    std::span<hpk_mesh_desc>            meshDescSpan    = {};

    void*                               diskAssetStore  = nullptr;
    u64                                 meshletOffsetFile = 0;

	float								moveSpeed		= 1.2f;
	float								mouseSensitivity = 0.002f;


	void Init( u64 hInst, u64 hWnd, u16 width, u16 height ) override;
	void RunLoop( double elapsedTime, bool isRunning, linear_arena& scratchArena, const ht_input_state& inputState ) override;
	void UploadAssets( linear_arena& virtualStack );
};

void ImGuiPrintPipelineStats( const void* pData )
{
    const borrowed_array<ht_pipeline_stats>& htPipelineStats = *( const borrowed_array<ht_pipeline_stats>* ) pData;
	for( const ht_pipeline_stats& ps : htPipelineStats )
	{
		if( ps.inputAssemblyVtxNum )
		{
		    ImGuiTxt( fixed_string<128>{ "{} IA vertices : {}", ps.name, ps.inputAssemblyVtxNum } );
		}
		if( ps.inputAssemblyPrimitiveNum )
		{
		    ImGuiTxt( fixed_string<128>{ "{} IA primitives : {}", ps.name, ps.inputAssemblyPrimitiveNum } );
		}
		if( ps.vsInvocationNum )
		{
		    ImGuiTxt( fixed_string<128>{ "{} VS invocations : {}", ps.name, ps.vsInvocationNum } );
		}
		if( ps.clipInvocationNum )
		{
		    ImGuiTxt( fixed_string<128>{ "{} Clip invocations : {}", ps.name, ps.clipInvocationNum } );
		}
		if( ps.clipPrimitiveNum )
		{
		    ImGuiTxt( fixed_string<128>{ "{} Clip primitives : {}", ps.name, ps.clipPrimitiveNum } );
		}
		if( ps.psInvocationCount )
		{
		    ImGuiTxt( fixed_string<128>{ "{} PS invocations : {}", ps.name, ps.psInvocationCount } );
		}
		if( ps.csInvocationCount )
		{
		    ImGuiTxt( fixed_string<128>{ "{} CS invocations : {}", ps.name, ps.csInvocationCount } );
		}
	}
}

void HTAssembleUI( renderer_dbg_draw& rndDbgFlags, void* pTimedZones, void*	pPipeStats )
{
	imgui_window imguiWnds[] = {
	    imgui_window{
	        .widgets = {
	            {
	                .pData  = pTimedZones,
	                .Action = []( const void* pData )
	                {
	                    const borrowed_array<ht_timed_zone>& timedZones = *( const borrowed_array<ht_timed_zone>* ) pData;
	                    for( const ht_timed_zone& tz : timedZones )
	                    {
	                        ImGuiTxt( fixed_string<128>{ "{} : {}", tz.name, tz.timeMs } );
	                    }
	                },
	                .type   = imgui_widget_type::TEXT
	            },
                { .pData = pPipeStats, .Action = ImGuiPrintPipelineStats, .type = imgui_widget_type::TEXT },
	            {
	                .pData = ( const void* ) g_pVirtualAllocator->committedInBytes,
	                .Action = []( const void* pData )
	                {
	                    ImGuiTxt( fixed_string<128>{ "Commited VMem MiB : {}", ( u64 ) pData / MB } );
	                },
	                .type   = imgui_widget_type::TEXT
	            }

	        },
            .name	= "Engine Stats",
            .flags	= ImGuiWindowFlags_NoScrollbar
        },
        imgui_window{
            .widgets = {
                { .name = " VBuffer PixelHash", .pData = &rndDbgFlags.vBuffPixelHash, .type = imgui_widget_type::CHECKBOX },
                { .name = " Draw Inst AABBs", .pData = &rndDbgFlags.dbgDraw, .type = imgui_widget_type::CHECKBOX },
                { .name = "Press F to freeze MainView", .type	= imgui_widget_type::TEXT },
                { .name = "Press L to toggle mesh LOD", .type = imgui_widget_type::TEXT },
                { .name = "Press K to toggle meshlet LOD", .type = imgui_widget_type::TEXT },
            },
            .name	= "Renderer Dbg Modes",
            .flags	= ImGuiWindowFlags_NoScrollbar
        }
	};

	ImGuiRenderUI( imguiWnds );
}


void helltech::Init( u64 hInst, u64 hWnd, u16 width, u16 height )
{
    g_GameArena     = linear_arena{ g_pVirtualAllocator->AllocVirtualBlock( 1 * GB, HtCurrentThreadIdx() ) };
    g_DebugArena    = linear_arena{ g_pVirtualAllocator->AllocVirtualBlock( 4 * MB, HtCurrentThreadIdx() ) };
    pGameArena      = &g_GameArena;
    pDebugArena     = &g_DebugArena;

    constexpr float fovRads     = ht::to_rads( 70.0f );
    constexpr float zNear	    = 0.5f;
    float2          viewportDim = ht::vec_cast<float2>( u16x2{ width, height } );

    mainActiveCam	= virtual_camera{ viewportDim, fovRads, zNear, IS_WORLD_RH };
    debugCam		= virtual_camera{ viewportDim, fovRads, zNear, IS_WORLD_RH };
    pRenderer       = MakeRenderer( *pPersistentArena );

    pRenderer->InitBackend( hInst, hWnd );

    imGuiCtx = im_gui_ctx{ width, height };

    // NOTE: arbitrary sized for now
    drawables       = borrowed_array{ ArenaNewArray<instance_desc>( *pGameArena, 10'000 ) };
    jobCache        = borrowed_array{ ArenaNewArray<upload_job_payload*>( *pGameArena, 1'000 ) };
    timedZones      = borrowed_array{ ArenaNewArray<ht_timed_zone>( *pDebugArena, 256 ) };
    pipelinesStats  = borrowed_array{ ArenaNewArray<ht_pipeline_stats>( *pDebugArena, 64 ) };

    // NOTE: hpk open
    constexpr char assetFilePath[] = "D:/3d models/caldera.hpk";

    void* hHpkFile = ht_os_create_file( assetFilePath, file_permission_t::READ,
        file_create_t::OPEN_IF_EXISTS, file_access_t::RAW_CONCURRENT );

    std::span ioReadBuff = g_pVirtualAllocator->AllocVirtualBlock( 384 * MB, HtCurrentThreadIdx() );
    defer { g_pVirtualAllocator->FreeVirtualBlock( ioReadBuff ); };

    HT_ASSERT( IsAlignedToPot( ( u64 ) &ioReadBuff[ 0 ], RAW_DISK_SECTOR_ALIGNMENT ) );

    hpk_file_header hpkHeader = {};
    {
        u64 readSize = FwdAlignPot( sizeof( hpk_file_header ), RAW_DISK_SECTOR_ALIGNMENT );
        SysFileRawReadNBytesBlocking( hHpkFile, 0, readSize, ioReadBuff );
        std::memcpy( &hpkHeader, &ioReadBuff[ 0 ], sizeof( hpk_file_header ) );
    }

    HT_ASSERT( std::bit_cast<u64>( HPK_MAGIC ) == hpkHeader.magic );
    HT_ASSERT( HPK_FORMAT_VERSION == hpkHeader.fileFormatVersion );
    // TODO: wtf ?
    //HT_ASSERT( HPK_CONTENT_VERSION == hpkHeader.contentVersion );

    auto LmbdReadHpkBuff = [ & ]<TRIVIAL_T T>( u64 dataOffset, u64 elemCount ) HT_LAMBDA_FORCEINLINE -> std::span<T>
    {
        std::span out   = ArenaNewArray<T>( *pGameArena, elemCount );
        std::span<u8> data = SysFileRawReadNBytesBlocking( hHpkFile, dataOffset, BYTE_COUNT( out ), ioReadBuff );
        std::ranges::copy( data, ( u8* ) &out[ 0 ] );

        return out;
    };

    sectorsSpan     = LmbdReadHpkBuff.operator()<hpk_sector_desc>( hpkHeader.firstSectorsOffsetInBytes, hpkHeader.sectorsCount );
    gpuInstSpan     = LmbdReadHpkBuff.operator()<gpu_instance>( hpkHeader.firstNodeOffsetInBytes, hpkHeader.nodeCount );
    meshDescSpan    = LmbdReadHpkBuff.operator()<hpk_mesh_desc>( hpkHeader.firstMeshDescOffsetInBytes, hpkHeader.meshDescCount );

    diskAssetStore      = hHpkFile;
    meshletOffsetFile   = hpkHeader.firstMeshletsOffsetInBytes;
}

void helltech::UploadAssets( linear_arena& scratchpadArena )
{
	ht_mem_scope memScope = { scratchpadArena };

    hpk_sector_desc mostBytesSec = *std::ranges::find_if( sectorsSpan, []( const auto& s ) {
        return ht::all( i16x2{ 1, 1 } == s.secID );
    } );

    std::span<const gpu_instance> nodes = gpuInstSpan.subspan( mostBytesSec.firstNodeIdx, mostBytesSec.nodeCount );

    // TODO: use our own
    ankerl::unordered_dense::set<u64> uniqueMeshes;
    for( const gpu_instance& n : nodes )
    {
        uniqueMeshes.emplace( n.meshIdx );
    }

    upload_job_payload* pPayload = HtMakeUploadPayload( std::size( uniqueMeshes ), nodes );
    for( const hpk_mesh_desc& meshDesc : ht::permuted_view( meshDescSpan, uniqueMeshes ) )
    {
        pPayload->meshUploads.push_back( { .hSlot = pRenderer->AllocMeshSlot( meshDesc ) } );
    }

    jobCache.push_back( pPayload );

    g_pJobSys->SubmitJob( [ pJob = jobCache.back(), pRI = pRenderer, diskAssetStore = diskAssetStore,
        meshletOffsetFile = meshletOffsetFile ]( linear_arena* arena )
    {
        pRI->UploadMeshes( &pJob->hUploadDoneSignal, pJob->meshUploads, diskAssetStore, meshletOffsetFile, *arena );
    } );

}

void helltech::RunLoop( double elapsedTime, bool isRunning, linear_arena& scratchArena, const ht_input_state& inputState )
{
	ht_mem_scope scope = { scratchArena };

	static bool vfsMounted = false;
	if( !vfsMounted )
	{
		UploadAssets( scratchArena );
		vfsMounted = true;
	}

	auto[ camMove, dRot ] = GetMoveCamAction( inputState, ( float ) elapsedTime, moveSpeed, mouseSensitivity );

	rndDbgFlags.freezeMainView	= inputState.IsButtonHeld( GLOB_ACTION_MAP.frustumDbg );
	rndDbgFlags.drawXRayMode	= inputState.IsButtonHeld( GLOB_ACTION_MAP.xrayDraw );

	if( inputState.IsButtonPressed( GLOB_ACTION_MAP.instCull ) )
	{
		rndDbgFlags.toggleInstCull = !rndDbgFlags.toggleInstCull;
	}
	if( inputState.IsButtonPressed( GLOB_ACTION_MAP.mltCull ) )
	{
		rndDbgFlags.toggleMltCull = !rndDbgFlags.toggleMltCull;
	}
	if( inputState.IsButtonPressed( GLOB_ACTION_MAP.toggleMeshLOD ) )
	{
		rndDbgFlags.toggleMeshLOD = !rndDbgFlags.toggleMeshLOD;
	}
	if( inputState.IsButtonPressed( GLOB_ACTION_MAP.toggleMltLOD ) )
	{
		rndDbgFlags.toggleMltLOD = !rndDbgFlags.toggleMltLOD;
	}

	mainActiveCam.Move( camMove, dRot );
	if( !rndDbgFlags.freezeMainView ) [[ likely ]] { debugCam = mainActiveCam; }

    /*
    inline_array<hpk_sector_desc, 256> inViewSectors = {};
    for( const hpk_sector_desc& sector : sectorsSpan )
    {
        constexpr float PLAYER_VIEW_RAD = PLAYER_LOAD_RADIUS_METERS + 0.5f * GRID_SECTOR_DIM_IN_METERS;
        float2 secCenter = ( ht::vec_cast<float2>( sector.secID ) + 0.5f ) * GRID_SECTOR_DIM_IN_METERS;
        // NOTE: .xz bc of the conventions
        if( ht::all( ht::abs( mainActiveCam.worldPos.xz - secCenter ) <= PLAYER_VIEW_RAD ) )
        // NOTE: must "unload" when we cross the "frontier" + 1 / RAD + 1 --> evict
        {
            inViewSectors.push_back( sector );
        }
    }

    for( const hpk_sector_desc& sec : inViewSectors )
    {
        std::span<world_node> secNodes = worldNodesSpan.subspan( sec.firstNodeIdx, sec.nodeCount );
    }
    */

	view_data dbgViewData   = ViewData( debugCam );
	view_data views[]       = { ViewData( mainActiveCam ), dbgViewData };

	float4x4 frustumMat     = DX_XMStoreFloat4x4A(
	    FrustumMatrixFromViewProj( DX_XMLoadFloat4x4A( dbgViewData.mainViewProj ) ) );

	imGuiCtx.UpdateTimeAndInputState( ( float ) elapsedTime, inputState );

	// NOTE: this is a temp thing and will work bc we have JUST 1 upload
	if( std::size( jobCache ) )
	{
		upload_job_payload* pPayload = jobCache[ 0 ];
		if( pRenderer->PollJobCompletion( &pPayload->hUploadDoneSignal ) )
		{
			//drawables.append_range( pPayload->entitiesToPromote );
		    jobCache.pop_back();

		    g_pVirtualAllocator->FreeVirtualBlock( { ( u8* ) pPayload, pPayload->allocSzInBytes } );
		}
	}

	timedZones.push_back( { .name = "CPU FrameMs: ", .timeMs = ( float )( elapsedTime * 1000.0 ) } );

	HTAssembleUI( rndDbgFlags, &timedZones, &pipelinesStats );

	timedZones.resize( 0 );
	pipelinesStats.resize( 0 );

	frame_data frameData = {
		.views 			= views,
		.instances 		= drawables,
		.frustTransf	= frustumMat,
		.elapsedSeconds = ( float ) elapsedTime,
		.dbgDrawFlags	= rndDbgFlags
	};

	gpu_data gpuData = { timedZones, pipelinesStats };
	pRenderer->HostFrames( frameData, scratchArena, gpuData );
}

helltech_interface* MakeHelltech( linear_arena& arena ) { return ArenaNew<helltech>( arena ); }