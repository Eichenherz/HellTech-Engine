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

// TODO: use our own
#include <ankerl/unordered_dense.h>


// NOTE: can't move or copy bc out stack holds pointers + the OS also holds pointers here !
struct hpk_asset_file
{
    using io_req_pool = inline_atomic_stack<ht_os_io_request, OS_MAX_ASYNC_IO_REQS_IN_FLIGHT>;

    void*       hIOPort = nullptr;
    void*       hFile   = nullptr;
    io_req_pool reqPool = {};

    hpk_asset_file() = default;
    hpk_asset_file( const char* filePath, u64 workerCount ) :
        hIOPort{ HtOsCreateIOCompletionPort( workerCount ) },
        hFile{ ht_os_create_file( filePath, file_perm_t::READ, file_create_t::OPEN_IF_EXISTS,
            file_access_t::CONCURRENT_UNBUFFERED, hIOPort ) }
    {}

    void ReadFile( u64 offsetInBytes, u64 sizeInBytes, linear_arena& refArena ){}

    NO_COPY(); NO_MOVE();
};


//==================CONSTEXPR===================//
constexpr float YAW_SIGN   = FSignOf( ht::dot( ht::cross( WORLD_UP,    WORLD_FWD ), -WORLD_LEFT ) );
constexpr float PITCH_SIGN = FSignOf( ht::dot( ht::cross( -WORLD_LEFT, WORLD_FWD ), -WORLD_UP ) );
//==============================================//

//===================GLOBALS====================//
static linear_arena g_GameArena     = {};
static linear_arena g_DebugArena    = {};
linear_arena*       pGameArena      = nullptr;
linear_arena*       pDebugArena     = nullptr;
hpk_asset_file*     pAssetFile      = {};
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
	float3				worldPos	= { 0.0f, 0.0f, 0.0f };
	float3				camViewDir	= {};
	float2				viewportDim	= {};
	PFN_LookAtCoord	    PfnLookAt	= nullptr;
	float				zNear		= NAN;
	// NOTE: pitch must be in [ -pi/2, pi/2 ]
	float				pitch		= 0.0f;
	float				yaw			= 0.0f;

    virtual_camera( float2 viewportDim, float radsYFov, float zNear, bool isRH ) :
        proj{ PerspRevZInfFarFromFovAndAspectRatio( radsYFov, viewportDim.x / viewportDim.y, zNear, isRH ) },
        viewportDim{ viewportDim }, PfnLookAt{ isRH ? MatLookAtRH : MatLookAtLH }, zNear{ zNear }
    {}

	void Move( float3 camMove, float2 dRot )
	{
		yaw     = ht::mod_angle( yaw + dRot.x );
		pitch   = std::clamp( pitch + dRot.y, -HT_ALMOST_HALF_PI, HT_ALMOST_HALF_PI );

		float3x3    rot         = RotMatFromPitchYawRoll( pitch, yaw, 0 );
		float3      newWorldPos = worldPos + ht::mul( ht::normalize( camMove ), rot );
		float3      camLookAt   = ht::mul( WORLD_FWD, rot );

		prevView    = view;
		view        = PfnLookAt( newWorldPos, newWorldPos + camLookAt, WORLD_UP );
		worldPos    = newWorldPos;
		camViewDir  = -camLookAt;
	}
};

view_data ViewData( const virtual_camera& virtCam )
{
    return {
        .proj			= virtCam.proj,
        .mainView		= virtCam.view,
        .prevView		= virtCam.prevView,
        .mainViewProj	= virtCam.view * virtCam.proj,
        .prevViewProj	= virtCam.prevView * virtCam.proj,
        .worldPos		= virtCam.worldPos,
        .zNear			= virtCam.zNear,
        // NOTE: this must not be negative for LH coords
        .camViewDir		= virtCam.camViewDir,
        .lodTarget		= ( 2.0f / virtCam.proj[ 1 ][ 1 ] ) * ( 1.0f / float( virtCam.viewportDim.y ) )
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
	using namespace DirectX;

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

	if( ht::all( float3{} != camMove ) )
	{
		camMove = ht::normalize( camMove ) * ( mvSpeed * elapsedTime );
	}

	float2 yawPitch = YAW_SIGN * mouseSensitivity * ( float2 ) inputState.dPosMouse;
	return { .camMove = camMove, .dRot = yawPitch };
}

// Job system
job_system_ctx::job_system_ctx()
    : queue{ ArenaNewArray<job_t>( *pPersistentArena, 128 ) } {}
void job_system_ctx::SubmitJob( job_t job )
{
	HT_ASSERT( queue.TryPush( job ) );
	SysSemaphoreRelease( sema, 1 );
}

// Uploads
struct upload_job_payload
{
	borrowed_array<mesh_upload_req>	meshUploads         = {};
	borrowed_array<instance_desc>	entitiesToPromote   = {};
	renderer_interface*			    pRI                 = nullptr;
	atomic_u64					    hUploadDoneSignal   = ~0ull; // NOTE: bc we check it against the timeline sema
    u64                             allocSzInBytes      = 0; // NOTE: we need this bc we are responsible for the alloc handle
};

static upload_job_payload* HtMakeUploadPayload( u64 maxMeshCap, u64 maxInstCap, renderer_interface* pRI )
{
    constexpr u64 headerSzInBytes = sizeof( upload_job_payload );

    u64 meshSzInBytes       = maxMeshCap * sizeof( mesh_upload_req );
    u64 instSzInBytes       = maxInstCap * sizeof( instance_desc );
    u64 requestSzInBytes    = std::max( BLOCK_SZ_IN_BYTES, headerSzInBytes + meshSzInBytes + instSzInBytes );

    std::span<u8> mem       = g_pVirtualAllocator->AllocVirtualBlock( requestSzInBytes, HtCurrentThreadIdx() );
    std::span<u8> meshMem   = mem.subspan( headerSzInBytes, meshSzInBytes );
    std::span<u8> instMem   = mem.subspan( headerSzInBytes + meshSzInBytes, instSzInBytes );

    upload_job_payload* pPayload = ( upload_job_payload* ) std::data( mem );
    *pPayload = {
        .meshUploads        = { FromBytes<mesh_upload_req>( meshMem ) },
        .entitiesToPromote  = { FromBytes<instance_desc>( instMem ) },
        .pRI                = pRI,
        .allocSzInBytes     = std::size( mem )
    };

    return pPayload;
}

void PfnRendererUploadJob( void* payload, linear_arena* arena )
{
	upload_job_payload* pJob = ( upload_job_payload* ) payload;
	pJob->pRI->UploadMeshes( &pJob->hUploadDoneSignal, pJob->meshUploads, *arena );
}

// TODO: use our own
using hpk_dense_mesh_set    = ankerl::unordered_dense::set<hpk_mesh_desc, hpk_mesh_desc_key, hpk_mesh_desc_key>;
using hpk_dense_sector_set  = ankerl::unordered_dense::set<hpk_sector_desc, hpk_sector_desc_key, hpk_sector_desc_key>;

// Engine
struct helltech final : helltech_interface
{
    hpk_dense_sector_set                secotrsSet      = {};
    hpk_dense_mesh_set                  meshDescSet     = {};
    std::span<hpk_lod_desc>             lodDescSpan     = {};

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

	float								moveSpeed		= 1.2f;
	float								mouseSensitivity = 0.002f;

	void Init( u64 hInst, u64 hWnd, u16 width, u16 height ) override;
	void RunLoop( double elapsedTime, bool isRunning, linear_arena& scratchArena,
	    const ht_input_state& inputState ) override;
	void UploadAssets( linear_arena& virtualStack );

};

void ImGuiPrintPipelineStats( const void* pData )
{
    const borrowed_array<ht_pipeline_stats>& htPipelineStats = *( const borrowed_array<ht_pipeline_stats>* ) pData;
	for( const ht_pipeline_stats& ps : htPipelineStats )
	{
		if( ps.inputAssemblyVtxNum ) ImGuiTxt( fixed_string<128>{ "{} IA vertices : {}",
			ps.name, ps.inputAssemblyVtxNum } );
		if( ps.inputAssemblyPrimitiveNum ) ImGuiTxt( fixed_string<128>{ "{} IA primitives : {}",
			ps.name, ps.inputAssemblyPrimitiveNum } );
		if( ps.vsInvocationNum ) ImGuiTxt( fixed_string<128>{ "{} VS invocations : {}",
			ps.name, ps.vsInvocationNum } );
		if( ps.clipInvocationNum ) ImGuiTxt( fixed_string<128>{ "{} Clip invocations : {}",
			ps.name, ps.clipInvocationNum } );
		if( ps.clipPrimitiveNum ) ImGuiTxt( fixed_string<128>{ "{} Clip primitives : {}",
			ps.name, ps.clipPrimitiveNum } );
		if( ps.psInvocationCount ) ImGuiTxt( fixed_string<128>{ "{} PS invocations : {}",
			ps.name, ps.psInvocationCount } );
		if( ps.csInvocationCount ) ImGuiTxt( fixed_string<128>{ "{} CS invocations : {}",
			ps.name, ps.csInvocationCount } );
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
    g_GameArena     = { g_pVirtualAllocator->AllocVirtualBlock( 64 * MB, HtCurrentThreadIdx() ) };
    g_DebugArena    = { g_pVirtualAllocator->AllocVirtualBlock( 4 * MB, HtCurrentThreadIdx() ) };
    pGameArena      = &g_GameArena;
    pDebugArena     = &g_DebugArena;

	constexpr float fovRads     = ht::to_rads( 70.0f );
	constexpr float zNear	    = 0.5f;
    float2          viewportDim = ht::vec_cast<float2>( u16x2{ width, height } );

	mainActiveCam	= virtual_camera{ viewportDim, fovRads, zNear, IS_WORLD_RH };
	debugCam		= virtual_camera{ viewportDim, fovRads, zNear, IS_WORLD_RH };
	pRenderer       = MakeRenderer( *pPersistentArena );

	pRenderer->InitBackend( hInst, hWnd );

	imGuiCtx = { width, height };

	constexpr char assetFilePath[] = "D:/3d models/caldera.hpk";
	pAssetFile = ArenaMake<hpk_asset_file>( *pPersistentArena, assetFilePath, g_NumCores );

    std::span<const u8> mem = HtOsCreateROFileMapping( pAssetFile->hFile );
    defer { HtOSUnmapView( mem ); }; // NOTE: we only do this to save VMem space although prolly not needed

    hpk_file_view view = HpkGetFileView( mem );

    secotrsSet  = { std::begin( view.sectors ), std::end( view.sectors ) };
    meshDescSet = { std::begin( view.meshes ), std::end( view.meshes ) };
    lodDescSpan = ArenaNewArray<hpk_lod_desc>( *pPersistentArena, std::size( view.sectors ) );
    std::ranges::copy( view.lods, std::ranges::begin( lodDescSpan ) );

    // NOTE: arbitrary sized for now
    drawables       = { ArenaNewArray<instance_desc>( *pGameArena, 10'000 ) };
    jobCache        = { ArenaNewArray<upload_job_payload*>( *pGameArena, 1'000 ) };
    timedZones      = { ArenaNewArray<ht_timed_zone>( *pDebugArena, 256 ) };
    pipelinesStats  = { ArenaNewArray<ht_pipeline_stats>( *pDebugArena, 64 ) };
}

// TODO: revisit this logic
void helltech::UploadAssets( linear_arena& scratchpadArena )
{
	ht_mem_scope memScope = { scratchpadArena };


    // TODO: use our own
	ankerl::unordered_dense::map<u64, HRNDMESH32> meshIdMap = {};
    meshIdMap.reserve( std::ranges::distance( meshFiles ) );

    u64 totalMeshCount = std::ranges::distance( meshFiles );
    u64 totalInstCount = 10'000; //std::ranges::distance( vec_of_vecs | std::views::join ); // NOTE: arbitrary size for now

    upload_job_payload* pPayload = HtMakeUploadPayload( totalMeshCount, totalInstCount, pRenderer );

	for( const vfs_path& vpath : meshFiles )
	{
		u64 pathHash = std::hash<vfs_path>{}( vpath );
		// TODO: might wanna check on content hash too
		if( meshIdMap.contains( pathHash ) ) continue;

		std::span<const u8> rawBytes    = vfs.GetFileByteView( vpath );
		hpk_mesh_view       mesh        = HpkDeserializeAsset<hpk_mesh_asset>( rawBytes );
		HRNDMESH32          hMesh       = pRenderer->AllocMeshComponent( mesh );

		pPayload->meshUploads.push_back( {
			.mltAsBytes			= AsBytes( mesh.meshlets ),
			.vtxPosAsBytes		= AsBytes( mesh.vtxPosBitstream ),
			.vtxAttrsAsBytes	= AsBytes( mesh.vertexAttrs ),
			.idxAsBytes			= AsBytes( mesh.indices ),
			.hSlot				= hMesh
		} );

		meshIdMap.emplace( pathHash, hMesh );
	}

	for( const vfs_path& vpath : levelFiles )
	{
		std::span<const u8> rawBytes    = vfs.GetFileByteView( vpath );
		hpk_level_view      lvl         = HpkDeserializeAsset<hpk_level_asset>( rawBytes );

		for( const world_node& node : lvl.nodes )
		{
			auto it = meshIdMap.find( node.meshHash );
			if( std::cend( meshIdMap ) == it ) continue;
			pPayload->entitiesToPromote.push_back( { .transform = node.toWorld, .meshIdx = it->second } );
		}
	}

    jobCache.push_back( pPayload );

    g_pJobSys->SubmitJob( { .PfnJob = PfnRendererUploadJob, .payload = jobCache.back() } );
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
	[[ likely ]]
	if( !rndDbgFlags.freezeMainView )
	{
		debugCam = mainActiveCam;
	}

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
			drawables.append_range( pPayload->entitiesToPromote );
		    jobCache.pop_back();

		    g_pVirtualAllocator->FreeVirtualBlock( { ( u8* ) pPayload, pPayload->allocSzInBytes }, 0 );
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

helltech_interface* MakeHelltech( linear_arena& arena ) { return ( helltech_interface* ) ArenaNew<helltech>( arena ); }