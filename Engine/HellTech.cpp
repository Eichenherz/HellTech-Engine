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
#include <System/sys_thread.h>

#include "zip_pack.h"

#include <ankerl/unordered_dense.h>


// Virtual camera
using PFN_XMLookAtCoord = DirectX::XMMATRIX ( XM_CALLCONV * ) (
	DirectX::FXMVECTOR eyePos,
	DirectX::FXMVECTOR focusPos,
	DirectX::FXMVECTOR upDir
);

struct virtual_camera
{
	float4x4			proj = {};
	float4x4			prevView = {};
	float4x4			prevViewProj = {};
	float3 				fwdBasis = {};
	float3 				upBasis = {};
	float3				worldPos = { 0.0f, 0.0f, 0.0f };

	PFN_XMLookAtCoord	LookAt = nullptr;

	// NOTE: pitch must be in [-pi/2,pi/2]
	float				pitch = 0.0f;
	float				yaw = 0.0f;

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

		XMVECTOR camLookAt = XMVector3Transform( XMLoadFloat3( &fwdBasis ),
			XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
		XMMATRIX view = LookAt( xmWorldPos, XMVectorAdd( xmWorldPos, camLookAt ),
			XMLoadFloat3( &upBasis ) );

		XMMATRIX xmProj = XMLoadFloat4x4A( &proj );

		// TODO: don't transpose as soon as we fix the HLSL mem read !
		return {
			.proj			= proj,
			.mainView		= DX_XMStoreFloat4x4( XMMatrixTranspose( view ) ),
			//.prevView		= DX_XMStoreFloat4x4(),
			.mainViewProj	= DX_XMStoreFloat4x4( XMMatrixTranspose( XMMatrixMultiply( view, xmProj ) ) ),
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
		.proj		= ProjWithRevZInfFarFromFovAndAspectRatio( radsYFov, aspectRatioWH, zNear, false ),
		.fwdBasis	= { 0.0f, 0.0f, 1.0f },
		.upBasis	= { 0.0f, 1.0f, 0.0f },
		.LookAt		= DirectX::XMMatrixLookAtLH
	};
}

inline virtual_camera MakeVirtualCameraWithProjRH( float radsYFov, float aspectRatioWH, float zNear )
{
	return {
		.proj		= ProjWithRevZInfFarFromFovAndAspectRatio( radsYFov, aspectRatioWH, zNear, true ),
		.fwdBasis	= { 0.0f, 0.0f, -1.0f },
		.upBasis	= { 0.0f, 1.0f, 0.0f },
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
	float				moveSpeed,
	float				elapsedTime,
	float				mouseSensitivity
) {
	using namespace DirectX;

	XMVECTOR camMove = XMVectorSet( 0, 0, 0, 0 );
	if( inputState.keyStates[ HT_SC_W ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, 1, 0 ) );
	if( inputState.keyStates[ HT_SC_A ] ) camMove = XMVectorAdd( camMove, XMVectorSet( -1, 0, 0, 0 ) );
	if( inputState.keyStates[ HT_SC_S ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 0, -1, 0 ) );
	if( inputState.keyStates[ HT_SC_D ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 1, 0, 0, 0 ) );
	if( inputState.keyStates[ HT_SC_SPACE ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, 1, 0, 0 ) );
	if( inputState.keyStates[ HT_SC_C ] ) camMove = XMVectorAdd( camMove, XMVectorSet( 0, -1, 0, 0 ) );

	if( !XMVector3Equal( camMove, XMVectorZero() ) )
	{
		camMove = XMVectorScale( XMVector3Normalize( camMove ), moveSpeed * elapsedTime );
	}

	float2 yawPitch = {
		( float ) inputState.mouseDx * mouseSensitivity,
		( float ) inputState.mouseDy * mouseSensitivity
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
struct renderer_upload_job
{
	std::vector<mesh_upload_req>	reqs;
	renderer_interface*				pRI;
};

void PfnRendererUploadJob( void* payload, virtual_arena* arena )
{
	renderer_upload_job* pJob = ( renderer_upload_job* ) payload;

	pJob->pRI->UploadMeshes( pJob->reqs, *arena );
	// TODO: use own arenas and allocators
	delete payload;
}

// Enigne
struct helltech final : helltech_interface
{
	virtual_arena						persistentArena = { 1 * GB };

	mmap_file							mmappedFile		= {};

    virtual_camera                      mainActiveCam   = {};
    virtual_camera                      debugCam        = {};

	gpu_data							gpuData			= {};

	im_gui_ctx							imGuiCtx		= {};
	std::vector<imgui_window>			imguiWnds;

	ht_engine_stats						engineStats		= {};

	ht_stretchybuff<gpu_instance>		flatSceneGraph	= {};

	std::unique_ptr<renderer_interface> pRenderer		= {};

	job_system_ctx*						pJobSys			= nullptr;

	float								moveSpeed		= 1.2f;
	float								mouseSensitivity = 0.002f;

	void Init( job_system_ctx* jobSystemCtx, u64 hInst, u64 hWnd, u16 width, u16 height ) override;
	void RunLoop( double elapsedTime, bool isRunning, virtual_arena& scratchArena, const input_state& inputState ) override;

	void UploadAssests( stack_adaptor<virtual_arena>& virtualStack );
};

void helltech::Init( job_system_ctx* jobSystemCtx, u64 hInst, u64 hWnd, u16 width, u16 height )
{
	constexpr float fovRads = DirectX::XMConvertToRadians( 70.0f );
	constexpr float zNear = 0.5f;

	float aspecRatioWH = float( width ) / float( height );

	mainActiveCam = MakeVirtualCameraWithProjRH( fovRads, aspecRatioWH, zNear );
	debugCam = MakeVirtualCameraWithProjRH( fovRads, aspecRatioWH, zNear );

	{
		view_data mainViewData = mainActiveCam.GetViewData();
		mainActiveCam.prevViewProj = mainViewData.mainViewProj;
	}

	pRenderer = MakeRenderer();

	pRenderer->InitBackend( hInst, hWnd );

	imGuiCtx = { width, height };
	imguiWnds.push_back( {
		.widgets = {
			imgui_widget {
				.name = "GPU ms: ",
				.pData = &engineStats.gpuMs,
				.Action = ImGuiPrintFloatAction,
				.type = imgui_widget_type::TEXT
			},
			imgui_widget {
				.name = "CPU frame ms: ",
				.pData = &engineStats.cpuFrameMs,
				.Action = ImGuiPrintFloatAction,
				.type = imgui_widget_type::TEXT
			}
		},
		.name	= "Engine Stats",
		.flags	= ImGuiWindowFlags_NoScrollbar
	} );

		// {
		//	.widgets = {
		//		imgui_widget {
		//			.name	= "Load HPK",
		//			.Action = ImGuiLoadFileAction,
		//			.type	= imgui_widget_type::BUTTON
		//	    }
		//	},
		//	.name	= "##bnt_load_hpk",
		//	.flags	= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
		//	ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse
		//} );

	// TODO: vfs
	constexpr char	assetFile[] = "D:/3d models/Nightclub Futuristic/nightclub_futuristic_pub_ambience_asset.hpk";
	//constexpr char	assetFile[] = "D:/3d models/cyberbaron.hpk";
	//constexpr char	assetFile[] = "D:/3d models/sponza.hpk";
	mmappedFile = SysCreateMmapFile( assetFile, file_permissions_bits::READ,
		file_create_flags::OPEN_IF_EXISTS, file_access_flags::RANDOM );

	pJobSys = jobSystemCtx;
}

void helltech::UploadAssests( stack_adaptor<virtual_arena>& virtualStack )
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

		HRNDMESH32 hMesh =  pRenderer->AllocMeshComponent();

		uploads.push_back( { .filepath = vpath, .htAsset = mesh, .hSlot = hMesh } );

		meshIdMap.emplace( pathHash, hMesh );
	}

	// TODO: use own arenas and allocators
	job_t job = {
		.PfnJob = PfnRendererUploadJob,
		.payload = new renderer_upload_job{ .reqs = std::move( uploads ), .pRI = pRenderer.get() }
	};
	pJobSys->SubmitJob( job );

	//ankerl::unordered_dense::map<u64, u32> texIdMap;
	//for( const vfs_path& vpath : texFiles )
	//{
	//	u64 pathHash = std::hash<vfs_path>{}( vpath );
	//	// TODO: might wanna check on content hash too
	//	if( std::cend( texIdMap ) != texIdMap.find( pathHash ) ) continue;
	//}

	// TODO: must reserve before alloc
	HT_ASSERT( 1 == std::ranges::distance( levelFiles ) );
	for( const vfs_path& vpath : levelFiles )
	{
		std::span<const u8> rawBytes = vfs.GetFileByteView( vpath );
		hellpack_level lvl = HpkReadBinaryBlob<hellpack_level>( rawBytes );

		flatSceneGraph = HtANewStretchyBuffFromArena<gpu_instance>( persistentArena, std::size( lvl.nodes ) );
		for( const world_node& node : lvl.nodes )
		{
			auto it = meshIdMap.find( node.meshHash );
			if( std::cend( meshIdMap ) == it ) continue;
			flatSceneGraph.push_back( { .transform = node.toWorld, .meshIdx = it->second } );
		}
	}
}

void helltech::RunLoop( double elapsedTime, bool isRunning, virtual_arena& scratchArena, const input_state& inputState )
{
	using namespace DirectX;

	stack_adaptor<virtual_arena> virtualStack = { scratchArena };

	static bool vfsMounted = false;
	if( !vfsMounted )
	{
		UploadAssests( virtualStack );
		vfsMounted = true;
	}

	auto[ camMove, dRot ] = GetMoveCamAction( inputState, elapsedTime, moveSpeed, mouseSensitivity );

	mainActiveCam.Move( camMove, dRot );
	debugCam.Move( camMove, dRot );

	imGuiCtx.UpdateTimeAndInputState( elapsedTime, inputState );

	std::pmr::vector<view_data> views{ &virtualStack };

	view_data mainViewData = mainActiveCam.GetViewData();

	[[likely]]
	if( !inputState.keyStates[ HT_SC_F ] )
	{
		views.push_back( mainViewData );
		mainActiveCam.prevViewProj = mainViewData.mainViewProj;
	}

	views.push_back( debugCam.GetViewData() );

	XMMATRIX frustMat = FrustumMatrixFromViewProj( XMLoadFloat4x4A( &mainViewData.mainViewProj ) );

	frame_data frameData = {
		.views 			= views,
		.instances 		= flatSceneGraph,
		.frustTransf	= DX_XMStoreFloat4x4( frustMat ),
		.elapsedSeconds = ( float ) elapsedTime,
		.freezeMainView = inputState.keyStates[ HT_SC_F ],
		.dbgDraw		= inputState.keyStates[ HT_SC_O ]
	};

	engineStats = { .gpuMs = 0.0f, .cpuFrameMs = ( float )( elapsedTime * 1000.0 ) };

	ImGuiRenderUI( imguiWnds );

	pRenderer->HostFrames( frameData, gpuData );
}

helltech_interface* MakeHelltech( virtual_arena& arena )
{
	return ( helltech_interface* ) ArenaNew<helltech>( arena );
}