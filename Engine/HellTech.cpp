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
	static constexpr float3 CAM_UP = { 0.0f, 1.0f, 0.0f };

	float4x4			proj			= {};
	float4x4			prevView		= {};
	float4x4			prevViewProj	= {};
	float3				worldPos		= { 0.0f, 0.0f, 0.0f };

	PFN_XMLookAtCoord	LookAt			= nullptr;

	// NOTE: pitch must be in [-pi/2,pi/2]
	float				pitch			= 0.0f;
	float				yaw				= 0.0f;

	inline void XM_CALLCONV Move( DirectX::XMVECTOR camMove, float2 dRot )
	{
		using namespace DirectX;

		yaw = XMScalarModAngle( yaw + dRot.x );
		pitch = std::clamp( pitch + dRot.y, -HT_ALMOST_HALF_PI, HT_ALMOST_HALF_PI );

		XMMATRIX tRotScale = XMMatrixRotationRollPitchYaw( pitch, yaw, 0 );
		XMVECTOR xmCamMove = XMVector3Transform( XMVector3Normalize( camMove ), tRotScale );
		worldPos = DX_XMStoreFloat3( XMVectorAdd( XMLoadFloat3( &worldPos ), xmCamMove ) );
	}

	inline view_data GetViewData() const
	{
		using namespace DirectX;

		XMVECTOR xmWorldPos = XMLoadFloat3( &worldPos );

		XMVECTOR camLookAt = XMVector3Transform( DX_XMLoadFloat3( WORLD_FWD ),
			XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
		XMMATRIX view = LookAt( xmWorldPos, XMVectorAdd( xmWorldPos, camLookAt ),
			DX_XMLoadFloat3( WORLD_UP ) );

		XMMATRIX xmProj = XMLoadFloat4x4A( &proj );

		return {
			.proj			= DX_XMStoreFloat4x4A( xmProj ),
			.mainView		= DX_XMStoreFloat4x4A( view ),
			//.prevView		= DX_XMStoreFloat4x4(),
			.mainViewProj	= DX_XMStoreFloat4x4A( XMMatrixMultiply( view, xmProj ) ),
			.prevViewProj	= prevViewProj,
			.worldPos		= worldPos,
			// NOTE: this must not be negative for LH coords
			.camViewDir		= DX_XMStoreFloat3( XMVectorNegate( camLookAt ) )
		};
	}
};

inline virtual_camera MakeVirtualCameraWithProjLH( float radsYFov, float aspectRatioWH, float zNear )
{
	return {
		.proj		= PerspRevZInfFarFromFovAndAspectRatioLH( radsYFov, aspectRatioWH, zNear ),
		.LookAt		= DirectX::XMMatrixLookAtLH
	};
}

inline virtual_camera MakeVirtualCameraWithProjRH( float radsYFov, float aspectRatioWH, float zNear )
{
	return {
		.proj		= PerspRevZInfFarFromFovAndAspectRatioRH( radsYFov, aspectRatioWH, zNear ),
		.LookAt		= DirectX::XMMatrixLookAtRH
	};
}

// Input
#include <System/Win32/win32_kbd_scancodes.h>

struct move_cam_action
{
	DirectX::XMVECTOR	camMove;
	float2				dRot;
};

inline move_cam_action GetMoveCamAction(
	const input_state&	inputState,
	float				elapsedTime,
	float				moveSpeed,
	float				mouseSensitivity
) {
	using namespace DirectX;

	XMVECTOR camMove = XMVectorSet( 0, 0, 0, 0 );
	if( inputState.keyStates[ HT_SC_W ] ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_FWD ) );
	if( inputState.keyStates[ HT_SC_A ] ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_LEFT ) );
	if( inputState.keyStates[ HT_SC_S ] ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_FWD ) );
	if( inputState.keyStates[ HT_SC_D ] ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_LEFT ) );
	if( inputState.keyStates[ HT_SC_SPACE ] ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( WORLD_UP ) );
	if( inputState.keyStates[ HT_SC_C ] ) camMove = XMVectorAdd( camMove, DX_XMLoadFloat3( -WORLD_UP ) );

	float mvSpeed = moveSpeed;
	if( inputState.keyStates[ HT_SC_LCTRL ] )
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
	return { .camMove = camMove, .dRot = yawPitch };
}

// Misc
struct ht_engine_stats
{
	float gpuMs;
	float cpuFrameMs;
};

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

	gpu_data							gpuData			= {};

	im_gui_ctx							imGuiCtx		= {};
	// TODO: no vector
	std::vector<imgui_window>			imguiWnds		= {};

	ht_engine_stats						engineStats		= {};
	renderer_dbg_draw					rndDbgFlags		= {};
	// TODO: no vector
	std::vector<instance_desc>			drawables		= {};
	// TODO: don't use unique ptr
	std::unique_ptr<renderer_interface> pRenderer		= {};

	job_system_ctx*						pJobSys			= nullptr;
	// TODO: no vector
	std::vector<upload_job_payload*>	jobCache		= {};

	float								moveSpeed		= 1.2f;
	float								mouseSensitivity = 0.002f;

	void Init( job_system_ctx* jobSystemCtx, u64 hInst, u64 hWnd, u16 width, u16 height ) override;
	void RunLoop( double elapsedTime, bool isRunning, virtual_arena& scratchArena, const input_state& inputState ) override;

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


void helltech::Init( job_system_ctx* jobSystemCtx, u64 hInst, u64 hWnd, u16 width, u16 height )
{
	constexpr float fovRads = DirectX::XMConvertToRadians( 70.0f );
	constexpr float zNear = 0.5f;

	float aspecRatioWH = float( width ) / float( height );

	if constexpr( IS_WORLD_RH )
	{
		mainActiveCam	= MakeVirtualCameraWithProjRH( fovRads, aspecRatioWH, zNear );
		debugCam		= MakeVirtualCameraWithProjRH( fovRads, aspecRatioWH, zNear );
	}
	else // IS_LH
	{
		mainActiveCam	= MakeVirtualCameraWithProjLH( fovRads, aspecRatioWH, zNear );
		debugCam		= MakeVirtualCameraWithProjLH( fovRads, aspecRatioWH, zNear );
	}

	pRenderer = MakeRenderer();

	pRenderer->InitBackend( hInst, hWnd );

	imGuiCtx = { width, height };
	imguiWnds.push_back( {
		.widgets = {
			imgui_widget {
				.name	= "GPU ms: ",
				.pData	= &engineStats.gpuMs,
				.Action = ImGuiPrintFloatAction,
				.type	= imgui_widget_type::TEXT
			},
			imgui_widget {
				.name	= "CPU frame ms: ",
				.pData	= &engineStats.cpuFrameMs,
				.Action = ImGuiPrintFloatAction,
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
			//imgui_widget {
			//	.name	= " VBuffer MltId",
			//	.pData	= &rndDbgFlags.vBuffMeshletId,
			//	.Action = nullptr,
			//	.type	= imgui_widget_type::CHECKBOX
			//},
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
			.mltAsBytes = AsBytes( mesh.meshlets ),
			.vtxAsBytes = AsBytes( mesh.vertices ),
			.triAsBytes = AsBytes( mesh.triangles ),
			.hSlot		= hMesh
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

void helltech::RunLoop( double elapsedTime, bool isRunning, virtual_arena& scratchArena, const input_state& inputState )
{
	using namespace DirectX;

	stack_adaptor<virtual_arena> virtualStack = { scratchArena };

	static bool vfsMounted = false;
	if( !vfsMounted )
	{
		UploadAssets( virtualStack );
		vfsMounted = true;
	}

	auto[ camMove, dRot ] = GetMoveCamAction( inputState, elapsedTime,
		moveSpeed, mouseSensitivity );
	rndDbgFlags.freezeMainView = inputState.keyStates[ HT_SC_F ];

	mainActiveCam.Move( camMove, dRot );
	[[likely]]
	if( !rndDbgFlags.freezeMainView )
	{
		debugCam.worldPos	= mainActiveCam.worldPos;
		debugCam.pitch		= mainActiveCam.pitch;
		debugCam.yaw		= mainActiveCam.yaw;
	}

	std::pmr::vector<view_data> views{ &virtualStack };

	view_data mainViewData = mainActiveCam.GetViewData();
	views.push_back( mainViewData );
	mainActiveCam.prevViewProj = mainViewData.mainViewProj;

	view_data dbgViewData = debugCam.GetViewData();
	views.push_back( dbgViewData );
	debugCam.prevViewProj = dbgViewData.mainViewProj;

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

	engineStats = { .gpuMs = 0.0f, .cpuFrameMs = ( float )( elapsedTime * 1000.0 ) };

	ImGuiRenderUI( imguiWnds );

	frame_data frameData = {
		.views 			= views,
		.instances 		= drawables,
		.frustTransf	= DX_XMStoreFloat4x4A( FrustumMatrixFromViewProj( XMLoadFloat4x4A( &dbgViewData.mainViewProj ) ) ),
		.elapsedSeconds = ( float ) elapsedTime,
		.dbgDrawFlags	= rndDbgFlags
	};

	pRenderer->HostFrames( frameData, gpuData );
}

helltech_interface* MakeHelltech( virtual_arena& arena )
{
	return ( helltech_interface* ) ArenaNew<helltech>( arena );
}