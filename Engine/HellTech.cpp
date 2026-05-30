#include "engine_platform_common.h"

#include <ht_core_types.h>
#include <ht_mem_arena.h>
#include <ht_stretchybuff.h>

#include "ht_math.h"

#include "ht_renderer_types.h"

#include "engine_types.h"

#include "im_gui.h"

#include <System/sys_file.h>
#include <System/sys_sync.h>

#include "zip_pack.h"

#include <ankerl/unordered_dense.h>

constexpr float YAW_SIGN   = FSignOf( DotProd( CrossProd( WORLD_UP,    WORLD_FWD ), -WORLD_LEFT ) );
constexpr float PITCH_SIGN = FSignOf( DotProd( CrossProd( -WORLD_LEFT, WORLD_FWD ), -WORLD_UP ) );

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
	PFN_XMLookAtCoord	LookAt		= nullptr;
	float				zNear		= NAN;
	// NOTE: pitch must be in [-pi/2,pi/2]
	float				pitch		= 0.0f;
	float				yaw			= 0.0f;

	inline void XM_CALLCONV Move( float3 camMove, float2 dRot )
	{
		using namespace DirectX;

		yaw = XMScalarModAngle( yaw + dRot.x );
		pitch = std::clamp( pitch + dRot.y, -HT_ALMOST_HALF_PI, HT_ALMOST_HALF_PI );

		XMMATRIX tRotScale = XMMatrixRotationRollPitchYaw( pitch, yaw, 0 );
		XMVECTOR xmCamMove = XMVector3Transform( XMVector3Normalize( DX_XMLoadFloat3( camMove ) ), tRotScale );
		XMVECTOR xmWorldPos = XMVectorAdd( XMLoadFloat3( &worldPos ), xmCamMove );
		XMVECTOR camLookAt = XMVector3Transform( DX_XMLoadFloat3( WORLD_FWD ),
			XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
		XMMATRIX xmView = LookAt( xmWorldPos, XMVectorAdd( xmWorldPos, camLookAt ),
			DX_XMLoadFloat3( WORLD_UP ) );

		prevView = view;
		view = DX_XMStoreFloat4x4A( xmView );
		worldPos = DX_XMStoreFloat3( xmWorldPos );
		camViewDir = DX_XMStoreFloat3( XMVectorNegate( camLookAt ) );
	}

	inline view_data GetViewData() const
	{
		using namespace DirectX;

		XMMATRIX xmProj = XMLoadFloat4x4A( &proj );
		XMMATRIX xmView = XMLoadFloat4x4A( &view );
		XMMATRIX xmPrevView = XMLoadFloat4x4A( &prevView );

		return {
			.proj			= DX_XMStoreFloat4x4A( xmProj ),
			.mainView		= view,
			.prevView		= prevView,
			.mainViewProj	= DX_XMStoreFloat4x4A( XMMatrixMultiply( xmView, xmProj ) ),
			.prevViewProj	= DX_XMStoreFloat4x4A( XMMatrixMultiply( xmPrevView, xmProj ) ),
			.worldPos		= worldPos,
			.zNear			= zNear,
			// NOTE: this must not be negative for LH coords
			.camViewDir		= camViewDir
		};
	}
};

template<bool IS_RH>
virtual_camera MakeVirtualCamera( float radsYFov, float aspectRatioWH, float zNear )
{
	if constexpr( IS_RH )
	{
		return {
			.proj	= PerspRevZInfFarFromFovAndAspectRatioRH( radsYFov, aspectRatioWH, zNear ),
			.LookAt = DirectX::XMMatrixLookAtRH,
			.zNear	= zNear
		};
	}
	else
	{
		return {
			.proj	= PerspRevZInfFarFromFovAndAspectRatioLH( radsYFov, aspectRatioWH, zNear ),
			.LookAt = DirectX::XMMatrixLookAtLH,
			.zNear	= zNear
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
};

constexpr ht_demo_action_map GLOB_ACTION_MAP = {
	.fwd		= HT_SC_W,
	.bwd		= HT_SC_S,
	.left		= HT_SC_A,
	.right		= HT_SC_D,
	.up			= HT_SC_SPACE,
	.down		= HT_SC_C,
	.slowDown	= HT_SC_LCTRL,
	.frustumDbg = HT_SC_F,
	.xrayDraw	= HT_SC_X,
	.instCull	= HT_SC_I,
	.mltCull	= HT_SC_M
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
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.fwd ) ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_FWD ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.left ) ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_LEFT ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.bwd ) ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_FWD ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.right ) ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_LEFT ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.up ) ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_UP ) );
	if( inputState.IsButtonDown( GLOB_ACTION_MAP.down ) ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_UP ) );

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
job_system_ctx::job_system_ctx() : sema{}, queue{ 128 } {}
void job_system_ctx::SubmitJob( job_t job )
{
	HT_ASSERT( queue.TryPush( job ) );
	SysSemaphoreRelease( sema, 1 );
}

// Uploads
// TODO: use own arenas and allocators
struct upload_job_payload
{
	std::vector<mesh_upload_req>	reqs;
	std::vector<instance_desc>		entitiesToPromote;
	renderer_interface*				pRI;
	HJOBFENCE32						hUpload;
};

void PfnRendererUploadJob( void* payload, virtual_arena* arena )
{
	upload_job_payload* pJob = ( upload_job_payload* ) payload;
	pJob->pRI->UploadMeshes( pJob->hUpload, pJob->reqs, *arena );
}

// Engine
struct helltech final : helltech_interface
{
	virtual_arena						persistentArena = { 1 * GB };

	mmap_file							mmappedFile		= {};

    virtual_camera                      mainActiveCam   = {};
    virtual_camera                      debugCam        = {};

	im_gui_ctx							imGuiCtx		= {};

	renderer_dbg_draw					rndDbgFlags		= {};
	// TODO: no vector
	std::vector<instance_desc>			drawables		= {};
	// TODO: don't use unique ptr
	std::unique_ptr<renderer_interface> pRenderer		= {};

	job_system_ctx*						pJobSys			= nullptr;
	// TODO: no vector
	std::vector<upload_job_payload*>	jobCache		= {};

	std::vector<ht_timed_zone>			timedZones		= {};
	std::vector<ht_pipeline_stats>		pipelinesStats	= {};

	float								moveSpeed		= 1.2f;
	float								mouseSensitivity = 0.002f;

	void Init( job_system_ctx* jobSystemCtx, u64 hInst, u64 hWnd, u16 width, u16 height ) override;
	void RunLoop( double elapsedTime, bool isRunning, virtual_arena& scratchArena, const ht_input_state& inputState ) override;

	// TODO: must use own memory
	inline upload_job_payload* IssueUploadBatch( std::vector<mesh_upload_req>&& uploadReqs, std::vector<instance_desc>&& entities )
	{
		// TODO: use own arenas and allocators
		upload_job_payload* pPayload = new upload_job_payload{
			.reqs				= MOV( uploadReqs ),
			.entitiesToPromote	= MOV( entities ),
			.pRI				= pRenderer.get(),
			.hUpload			= pRenderer->AllocJobFence()
		};

		pJobSys->SubmitJob( { .PfnJob = PfnRendererUploadJob, .payload = pPayload } );

		return pPayload;
	}
	void UploadAssets( stack_adaptor<virtual_arena>& virtualStack );

};

void ImGuiPrintTimedZones( const void* pData )
{
	const std::vector<ht_timed_zone>& timedZones = *( const std::vector<ht_timed_zone>* ) pData;
	for( const ht_timed_zone& tz : timedZones )
	{
		ImGui::Text( "%-20s %.5f ms", ( const char* ) tz.name, tz.timeMs );
	}
}

void ImGuiPrintPipelineStats( const void* pData )
{
	const std::vector<ht_pipeline_stats>& pipeStats = *( const std::vector<ht_pipeline_stats>* ) pData;
	for( const ht_pipeline_stats& ps : pipeStats )
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

// TODO: no vector
void HTAssembleUI(
	renderer_dbg_draw						rndDbgFlags,
	const std::vector<ht_timed_zone>&		timedZones,
	const std::vector<ht_pipeline_stats>&	pipeStats
) {
	std::vector<imgui_window> imguiWnds;
	imguiWnds.push_back( {
		.widgets = {
			imgui_widget {
				.name	= "",
				.pData	= &timedZones, // NOTE: this is a local
				.Action = ImGuiPrintTimedZones,
				.type	= imgui_widget_type::TEXT
			},
			imgui_widget {
				.name	= "",
				.pData	= &pipeStats, // NOTE: this is a local
				.Action = ImGuiPrintPipelineStats,
				.type	= imgui_widget_type::TEXT
			}
		},
		.name	= "Engine Stats",
		.flags	= ImGuiWindowFlags_NoScrollbar
	} );

	imguiWnds.push_back( {
		.widgets = {
			imgui_widget {
				.name	= " VBuffer PixelHash",
				.pData	= &rndDbgFlags.vBuffPixelHash,
				.Action = nullptr,
				.type	= imgui_widget_type::CHECKBOX
			},
			imgui_widget {
				.name	= " Draw Inst AABBs",
				.pData	= &rndDbgFlags.dbgDraw,
				.Action = nullptr,
				.type	= imgui_widget_type::CHECKBOX
			},
			imgui_widget {
				.name	= "Press F to freeze MainView",
				.pData	= nullptr,
				.Action = nullptr,
				.type	= imgui_widget_type::TEXT
			},
		},
		.name	= "Renderer Dbg Modes",
		.flags	= ImGuiWindowFlags_NoScrollbar
	} );

	ImGuiRenderUI( imguiWnds );
}


void helltech::Init( job_system_ctx* jobSystemCtx, u64 hInst, u64 hWnd, u16 width, u16 height )
{
	constexpr float fovRads = DirectX::XMConvertToRadians( 70.0f );
	constexpr float zNear = 0.5f;

	float aspecRatioWH = float( width ) / float( height );

	mainActiveCam = MakeVirtualCamera<IS_WORLD_RH>( fovRads, aspecRatioWH, zNear );
	debugCam = MakeVirtualCamera<IS_WORLD_RH>( fovRads, aspecRatioWH, zNear );

	pRenderer = MakeRenderer();

	pRenderer->InitBackend( hInst, hWnd );

	imGuiCtx = { width, height };

	// TODO: vfs
	//constexpr char	assetFile[] = "D:/3d models/Nightclub Futuristic/nightclub_futuristic_pub_ambience_asset.hpk";
	//constexpr char	assetFile[] = "D:/3d models/Nightclub Futuristic/nightclub_no_flicker_group_question_mark.hpk";
	constexpr char assetFile[] = "D:/3d models/bistro.hpk";
	//constexpr char	assetFile[] = "D:/3d models/cyberbaron/cyberbaron.hpk";
	//constexpr char	assetFile[] = "D:/3d models/sponza.hpk";
	mmappedFile = SysCreateMmapFile( assetFile, file_permissions_bits::READ,
		file_create_flags::OPEN_IF_EXISTS, file_access_flags::RANDOM );

	pJobSys = jobSystemCtx;
}

// TODO: revisit this logic
void helltech::UploadAssets( stack_adaptor<virtual_arena>& virtualStack )
{
	// TODO: vfs
	vfs_zip_mem	 vfs = { mmappedFile };

	auto meshFiles = vfs.files | std::views::keys | std::views::filter(
	[] ( const vfs_path& vpath ) { return ( nullptr != std::strstr( std::data( vpath ), ".mesh" ) ); } );

	//auto texFiles = vfs.files | std::views::keys | std::views::filter(
	//	[] ( const vfs_path& vpath ) { return ( nullptr != std::strstr( std::data( vpath ), ".dds" ) ); } );

	auto levelFiles = vfs.files | std::views::keys | std::views::filter(
	[] ( const vfs_path& vpath ) { return ( nullptr != std::strstr( std::data( vpath ), ".lvl" ) ); } );

	ankerl::unordered_dense::pmr::map<u64, HRNDMESH32> meshIdMap{ &virtualStack };
	meshIdMap.reserve( std::ranges::distance( meshFiles ) );

	std::vector<mesh_upload_req> uploads;
	for( const vfs_path& vpath : meshFiles )
	{
		u64 pathHash = std::hash<vfs_path>{}( vpath );
		// TODO: might wanna check on content hash too
		if( std::cend( meshIdMap ) != meshIdMap.find( pathHash ) ) continue;

		std::span<const u8> rawBytes = vfs.GetFileByteView( vpath );
		hellpack_mesh_asset mesh = HpkReadBinaryBlob<hellpack_mesh_asset>( rawBytes );

		HRNDMESH32 hMesh = pRenderer->AllocMeshComponent( mesh );

		uploads.push_back( {
			.mltAsBytes			= AsBytes( mesh.meshlets ),
			.vtxPosAsBytes		= AsBytes( mesh.vtxPosBitstream ),
			.vtxAttrsAsBytes	= AsBytes( mesh.vertexAttrs ),
			.triAsBytes			= AsBytes( mesh.triangles ),
			.hSlot				= hMesh
		} );

		meshIdMap.emplace( pathHash, hMesh );
	}

	//ankerl::unordered_dense::map<u64, u32> texIdMap;
	//for( const vfs_path& vpath : texFiles )
	//{
	//	u64 pathHash = std::hash<vfs_path>{}( vpath );
	//	// TODO: might wanna check on content hash too
	//	if( std::cend( texIdMap ) != texIdMap.find( pathHash ) ) continue;
	//}

	std::vector<instance_desc> entities;
	entities.reserve( std::ranges::distance( levelFiles ) );

	for( const vfs_path& vpath : levelFiles )
	{
		std::span<const u8> rawBytes = vfs.GetFileByteView( vpath );
		hellpack_level lvl = HpkReadBinaryBlob<hellpack_level>( rawBytes );

		entities.reserve( std::size( entities ) + std::size( lvl.nodes ) );
		for( const world_node& node : lvl.nodes )
		{
			auto it = meshIdMap.find( node.meshHash );
			if( std::cend( meshIdMap ) == it ) continue;
			entities.push_back( { .transform = node.toWorld, .meshIdx = it->second } );
		}
	}

	jobCache.push_back( IssueUploadBatch( MOV( uploads ), MOV( entities ) ) );
}

void helltech::RunLoop( double elapsedTime, bool isRunning, virtual_arena& scratchArena, const ht_input_state& inputState )
{
	using namespace DirectX;

	stack_adaptor<virtual_arena> virtualStack = { scratchArena };

	static bool vfsMounted = false;
	if( !vfsMounted )
	{
		UploadAssets( virtualStack );
		vfsMounted = true;
	}

	auto[ camMove, dRot ] = GetMoveCamAction( inputState, ( float ) elapsedTime, moveSpeed, mouseSensitivity );

	rndDbgFlags.freezeMainView = inputState.IsButtonHeld( GLOB_ACTION_MAP.frustumDbg );
	rndDbgFlags.drawXRayMode = inputState.IsButtonHeld( GLOB_ACTION_MAP.xrayDraw );

	if( inputState.IsButtonPressed( GLOB_ACTION_MAP.instCull ) )
	{
		rndDbgFlags.toggleInstCull = !rndDbgFlags.toggleInstCull;
	}
	if( inputState.IsButtonPressed( GLOB_ACTION_MAP.mltCull ) )
	{
		rndDbgFlags.toggleMltCull = !rndDbgFlags.toggleMltCull;
	}

	mainActiveCam.Move( camMove, dRot );
	[[likely]]
	if( !rndDbgFlags.freezeMainView )
	{
		debugCam = mainActiveCam;
	}

	std::pmr::vector<view_data> views{ &virtualStack };
	views.push_back( mainActiveCam.GetViewData() );

	view_data dbgViewData = debugCam.GetViewData();
	views.push_back( dbgViewData );

	float4x4 frustumMat = DX_XMStoreFloat4x4A( FrustumMatrixFromViewProj( XMLoadFloat4x4A( &dbgViewData.mainViewProj ) ) );

	imGuiCtx.UpdateTimeAndInputState( ( float ) elapsedTime, inputState );

	// NOTE: this is a temp thing and will work bc we have JUST 1 upload
	if( std::size( jobCache ) )
	{
		upload_job_payload* pPayload = jobCache[ 0 ];
		if( pRenderer->PollJobFenceAndRemoveOnCompletion( pPayload->hUpload, 100'000 ) )
		{
			drawables.reserve( std::size( drawables ) + std::size( pPayload->entitiesToPromote ) );
			drawables.append_range( pPayload->entitiesToPromote );
			// TODO: use own arenas and allocators
			delete pPayload;
			jobCache.pop_back();
		}
	}

	// here we must the drawables instances

	timedZones.push_back( { .name = "CPU FrameMs: ", .timeMs = ( float )( elapsedTime * 1000.0 ) } );

	HTAssembleUI( rndDbgFlags, timedZones, pipelinesStats );

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

helltech_interface* MakeHelltech( virtual_arena& arena )
{
	return ArenaNew<helltech>( arena );
}