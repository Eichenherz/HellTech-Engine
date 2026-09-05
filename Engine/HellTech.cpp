#include <ht_core_types.h>
#include "engine_platform_api.h"
#include "engine_types.h"

#include <ht_memory.h>
#include <ht_vector.h>
#include <ht_math.h>

#include "ht_renderer_types.h"

#include <ht_serialization.h>

#include "im_gui.h"

#include <System/sys_file.h>
#include <System/sys_sync.h>

#include "zip_pack.h"

// TODO: use our own
#include <ankerl/unordered_dense.h>

//==================CONSTEXPR===================//
constexpr float YAW_SIGN   = FSignOf( DotProd( CrossProd( WORLD_UP,    WORLD_FWD ), -WORLD_LEFT ) );
constexpr float PITCH_SIGN = FSignOf( DotProd( CrossProd( -WORLD_LEFT, WORLD_FWD ), -WORLD_UP ) );
//==============================================//

//===================GLOBALS====================//
static linear_arena g_GameArena     = {};
static linear_arena g_DebugArena    = {};
linear_arena*       pGameArena      = nullptr;
linear_arena*       pDebugArena     = nullptr;
//==============================================//

// Virtual camera
using PFN_XMLookAtCoord = DirectX::XMMATRIX ( XM_CALLCONV * ) (
	DirectX::FXMVECTOR eyePos,
	DirectX::FXMVECTOR focusPos,
	DirectX::FXMVECTOR upDir
);

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
	PFN_XMLookAtCoord	LookAt		= nullptr;
	float				zNear		= NAN;
	// NOTE: pitch must be in [ -pi/2, pi/2 ]
	float				pitch		= 0.0f;
	float				yaw			= 0.0f;

	void XM_CALLCONV Move( float3 camMove, float2 dRot )
	{
		using namespace DirectX;

		yaw     = XMScalarModAngle( yaw + dRot.x );
		pitch   = std::clamp( pitch + dRot.y, -HT_ALMOST_HALF_PI, HT_ALMOST_HALF_PI );

		XMMATRIX tRotScale  = XMMatrixRotationRollPitchYaw( pitch, yaw, 0 );
		XMVECTOR xmCamMove  = XMVector3Transform( XMVector3Normalize( DX_XMLoadFloat3( camMove ) ), tRotScale );
		XMVECTOR xmWorldPos = XMVectorAdd( XMLoadFloat3( &worldPos ), xmCamMove );
		XMVECTOR camLookAt  = XMVector3Transform( DX_XMLoadFloat3( WORLD_FWD ),
			XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
		XMMATRIX xmView     = LookAt( xmWorldPos, XMVectorAdd( xmWorldPos, camLookAt ),
			DX_XMLoadFloat3( WORLD_UP ) );

		prevView    = view;
		view        = DX_XMStoreFloat4x4A( xmView );
		worldPos    = DX_XMStoreFloat3( xmWorldPos );
		camViewDir  = DX_XMStoreFloat3( XMVectorNegate( camLookAt ) );
	}

	view_data GetViewData() const
	{
		using namespace DirectX;

		XMMATRIX xmProj     = XMLoadFloat4x4A( &proj );
		XMMATRIX xmView     = XMLoadFloat4x4A( &view );
		XMMATRIX xmPrevView = XMLoadFloat4x4A( &prevView );

		float4x4 proj4x4    = DX_XMStoreFloat4x4A( xmProj );

		return {
			.proj			= proj4x4,
			.mainView		= view,
			.prevView		= prevView,
			.mainViewProj	= DX_XMStoreFloat4x4A( XMMatrixMultiply( xmView, xmProj ) ),
			.prevViewProj	= DX_XMStoreFloat4x4A( XMMatrixMultiply( xmPrevView, xmProj ) ),
			.worldPos		= worldPos,
			.zNear			= zNear,
			// NOTE: this must not be negative for LH coords
			.camViewDir		= camViewDir,
			.lodTarget		= ( 2.0f / proj( 1, 1 ) ) * ( 1.0f / float( viewportDim.y ) )
		};
	}
};

template<bool IS_RH>
virtual_camera MakeVirtualCamera( float2 viewportDim, float radsYFov, float zNear )
{
	float aspectRatioWH = viewportDim.x / viewportDim.y;

	if constexpr( IS_RH )
	{
		return {
			.proj			= PerspRevZInfFarFromFovAndAspectRatioRH( radsYFov, aspectRatioWH, zNear ),
			.viewportDim	= viewportDim,
			.LookAt			= DirectX::XMMatrixLookAtRH,
			.zNear			= zNear
		};
	}
	else
	{
		return {
			.proj			= PerspRevZInfFarFromFovAndAspectRatioLH( radsYFov, aspectRatioWH, zNear ),
			.viewportDim	= viewportDim,
			.LookAt			= DirectX::XMMatrixLookAtLH,
			.zNear			= zNear
		};
	}
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

	XMVECTOR camMove = XMVectorSet( 0, 0, 0, 0 );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.fwd ) )    camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_FWD ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.left ) )   camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_LEFT ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.bwd ) )    camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_FWD ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.right ) )  camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_LEFT ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.up ) )     camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_UP ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.down ) )   camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_UP ) );

	float mvSpeed = moveSpeed;
	if( inputState.IsButtonHeld( GLOB_ACTION_MAP.slowDown ) )
	{
		mvSpeed *= 0.4f;
	}

	if( !XMVector3Equal( camMove, XMVectorZero() ) )
	{
		camMove = XMVectorScale( XMVector3Normalize( camMove ), mvSpeed * elapsedTime );
	}

	float2 yawPitch = {
		YAW_SIGN * ( float ) inputState.mouseDx * mouseSensitivity,
		PITCH_SIGN * ( float ) inputState.mouseDy * mouseSensitivity
	};
	return { .camMove = DX_XMStoreFloat3( camMove ), .dRot = yawPitch };
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
	borrowed_vector<mesh_upload_req>	meshUploads         = {};
	borrowed_vector<instance_desc>	    entitiesToPromote   = {};
	renderer_interface*			        pRI                 = nullptr;
	HJOBFENCE32					        hUpload             = ~0u;
    u64                                 allocSzInBytes      = 0; // NOTE: we need this bc we are responsible for the alloc handle
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
        .hUpload            = pRI->AllocJobFence(),
        .allocSzInBytes     = std::size( mem )
    };

    return pPayload;
}

void PfnRendererUploadJob( void* payload, linear_arena* arena )
{
	upload_job_payload* pJob = ( upload_job_payload* ) payload;
	pJob->pRI->UploadMeshes( pJob->hUpload, pJob->meshUploads, *arena );
}

// Engine
struct helltech final : helltech_interface
{
	mmap_file							memMappedFile	= {};

    virtual_camera                      mainActiveCam   = {};
    virtual_camera                      debugCam        = {};

	im_gui_ctx							imGuiCtx		= {};

	renderer_dbg_draw					rndDbgFlags		= {};
	renderer_interface*                 pRenderer		= {};
    // NOTE: these are hard capped, we don't care to grow free the mem OS will do it for us on program exit
    borrowed_vector<instance_desc>		drawables		= {};
	borrowed_vector<upload_job_payload*>jobCache		= {};

	borrowed_vector<ht_timed_zone>		timedZones		= {};
	borrowed_vector<ht_pipeline_stats>	pipelinesStats	= {};

	float								moveSpeed		= 1.2f;
	float								mouseSensitivity = 0.002f;

	void Init( u64 hInst, u64 hWnd, u16 width, u16 height ) override;
	void RunLoop( double elapsedTime, bool isRunning, linear_arena& scratchArena,
	    const ht_input_state& inputState ) override;
	void UploadAssets( linear_arena& virtualStack );

};

void ImGuiPrintTimedZones( const void* pData )
{
    const borrowed_vector<ht_timed_zone>& timedZones = *( const borrowed_vector<ht_timed_zone>* ) pData;
	for( const ht_timed_zone& tz : timedZones )
	{
		ImGui::Text( "%-20s %.5f ms", ( const char* ) tz.name, tz.timeMs );
	}
}

void ImGuiPrintPipelineStats( const void* pData )
{
    const borrowed_vector<ht_pipeline_stats>& htPipelineStats = *( const borrowed_vector<ht_pipeline_stats>* ) pData;
	for( const ht_pipeline_stats& ps : htPipelineStats )
	{
		if( 0 != ps.inputAssemblyVtxNum ) ImGui::Text( "%-20s %-24s %llu", ( const char* ) ps.name, "IA vertices",
			ps.inputAssemblyVtxNum );
		if( 0 != ps.inputAssemblyPrimitiveNum ) ImGui::Text( "%-20s %-24s %llu", ( const char* ) ps.name, "IA primitives",
			ps.inputAssemblyPrimitiveNum );
		if( 0 != ps.vsInvocationNum ) ImGui::Text( "%-20s %-24s %llu", ( const char* ) ps.name, "VS invocations",
			ps.vsInvocationNum );
		if( 0 != ps.clipInvocationNum ) ImGui::Text( "%-20s %-24s %llu", ( const char* ) ps.name, "Clip invocations",
			ps.clipInvocationNum );
		if( 0 != ps.clipPrimitiveNum ) ImGui::Text( "%-20s %-20s %llu", ( const char* ) ps.name, "Clip primitives",
			ps.clipPrimitiveNum );
		if( 0 != ps.psInvocationCount ) ImGui::Text( "%-20s %-20s %llu", ( const char* ) ps.name, "PS invocations",
			ps.psInvocationCount );
		if( 0 != ps.csInvocationCount ) ImGui::Text( "%-20s %-20s %llu", ( const char* ) ps.name, "CS invocations",
			ps.csInvocationCount );
	}
}

void HTAssembleUI( renderer_dbg_draw& rndDbgFlags, void* pTimedZones, void*	pPipeStats )
{
	imgui_window imguiWnds[] = {
	    imgui_window{
	        .widgets = {
	            { .pData = pTimedZones, .Action = ImGuiPrintTimedZones, .type = imgui_widget_type::TEXT },
                { .pData = pPipeStats, .Action = ImGuiPrintPipelineStats, .type = imgui_widget_type::TEXT }
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

	constexpr float fovRads = DirectX::XMConvertToRadians( 70.0f );
	constexpr float zNear	= 0.5f;

	mainActiveCam	= MakeVirtualCamera<IS_WORLD_RH>( { float( width ), float( height ) }, fovRads, zNear );
	debugCam		= MakeVirtualCamera<IS_WORLD_RH>( { float( width ), float( height ) }, fovRads, zNear );
	pRenderer       = MakeRenderer( *pPersistentArena );

	pRenderer->InitBackend( hInst, hWnd );

	imGuiCtx = { width, height };

	// TODO: vfs
	//constexpr char	assetFile[] = "D:/3d models/Nightclub Futuristic/nightclub_futuristic_pub_ambience_asset.hpk";
	constexpr char assetFile[] = "D:/3d models/bistro.hpk";
	//constexpr char	assetFile[] = "D:/3d models/cyberbaron/cyberbaron.hpk";
	//constexpr char	assetFile[] = "D:/3d models/sponza.hpk";
	memMappedFile = SysCreateMmapFile( assetFile, file_permissions_bits::READ,
		file_create_flags::OPEN_IF_EXISTS, file_access_flags::RANDOM );

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
	// TODO: vfs
	vfs_zip_mem	 vfs = { memMappedFile };

    auto LmbdHasExt = []( std::string_view ext )
    {
        return std::views::filter( [ ext ]( std::string_view path ) { return path.ends_with( ext ); } );
    };

	auto meshFiles  = vfs.files | std::views::keys | LmbdHasExt( ".mesh" );
	//auto texFiles   = vfs.files | std::views::keys | LmbdHasExt( ".dds" );
	auto levelFiles = vfs.files | std::views::keys | LmbdHasExt( ".lvl" );

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

    pJobSys->SubmitJob( { .PfnJob = PfnRendererUploadJob, .payload = jobCache.back() } );
}

void helltech::RunLoop( double elapsedTime, bool isRunning, linear_arena& scratchArena, const ht_input_state& inputState )
{
	using namespace DirectX;

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
	[[likely]]
	if( !rndDbgFlags.freezeMainView )
	{
		debugCam = mainActiveCam;
	}

	view_data dbgViewData   = debugCam.GetViewData();
	view_data views[]       = { mainActiveCam.GetViewData(), dbgViewData };

	float4x4 frustumMat     = DX_XMStoreFloat4x4A(
	    FrustumMatrixFromViewProj( XMLoadFloat4x4A( &dbgViewData.mainViewProj ) ) );

	imGuiCtx.UpdateTimeAndInputState( ( float ) elapsedTime, inputState );

	// NOTE: this is a temp thing and will work bc we have JUST 1 upload
	if( std::size( jobCache ) )
	{
		upload_job_payload* pPayload = jobCache[ 0 ];
		if( pRenderer->PollJobFenceAndRemoveOnCompletion( pPayload->hUpload, 100'000 ) )
		{
			drawables.append_range( pPayload->entitiesToPromote );
		    jobCache.pop_back();

		    g_pVirtualAllocator->FreeVirtualBlock( { ( u8* ) pPayload, pPayload->allocSzInBytes }, 0 );
		}
	}

	// DBG
	//static u64 drawablesCount = 0;
	//drawablesCount += inputState.IsButtonPressed( HT_SC_J );
	//drawablesCount -= inputState.IsButtonPressed( HT_SC_K );
	//
	//std::vector<instance_desc> drw;
	//// here we must the drawables instances
	//if( std::size( drawables ) >= drawablesCount )
	//{
	//	for( u64 i = 0; i < drawablesCount; i++ )
	//	{
	//		drw.push_back( drawables[ i ] );
	//	}
	//}
	// !DBG

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