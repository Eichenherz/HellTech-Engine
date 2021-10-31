#define DX12_DEBUG

// TODO: don't really need this
#include <vector>
#include "sys_os_api.h"

// TODO: use C interface ?
//#define CINTERFACE
#include <include\d3d12.h>
#include <include\d3d12SDKLayers.h>

#include <unknwn.h>
#include <winerror.h>
#include <wrl/client.h>
#include <comdef.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <dxcapi.h>

#define HR_CHECK( func )												\
do{																		\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR"\nERR: ";	\
	HRESULT hr = func;													\
	if( !SUCCEEDED( hr ) ){                                             \
        _com_error err = { hr };                                        \
		char dbgStr[1024] = {};											\
		strcat_s( dbgStr, sizeof( dbgStr ), DEV_ERR_STR );				\
		strcat_s( dbgStr, sizeof( dbgStr ), err.ErrorMessage() );	    \
		SysErrMsgBox( dbgStr );											\
		constexpr UINT32 behaviour = MB_OK | MB_ICONERROR | MB_APPLMODAL; \
        MessageBox( 0, dbgStr, 0, behaviour );                          \
		abort();														\
	}																	\
}while( 0 )	

#ifdef DX12_DEBUG

#include <iostream>

void Dx12ErrorCallback(
	D3D12_MESSAGE_CATEGORY Category,
	D3D12_MESSAGE_SEVERITY Severity,
	D3D12_MESSAGE_ID ID,
	LPCSTR pDescription,
	void* pContext
){
	std::cout << ">>>DX12_ERROR<<<\n" << Category << '\n' << Severity << '\n' << pDescription << '\n';
}
#endif

// NOTE: Dx12 Agility SDK
extern "C" { __declspec( dllexport ) extern const UINT32 D3D12SDKVersion = 4; }
extern "C" { __declspec( dllexport ) extern LPCSTR D3D12SDKPath = ".\\D3D12\\"; }

// TODO: not global ?
PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceProc;



struct dx12_device
{
	ID3D12Device9* pDevice = 0;
	ID3D12CommandQueue* pCmdQueue = 0;
	ID3D12Fence1* pFence = 0;
};

inline static dx12_device Dx12CreateDeviceContext( IDXGIFactory7* pDxgiFactory )
{
	ID3D12Device9* pDevice = 0;
	IDXGIAdapter4* pGpu = 0;
	DXGI_ADAPTER_DESC3 pGpuInfo = {};
	for( UINT32 i = 0; 
		 pDxgiFactory->EnumAdapterByGpuPreference( i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS( &pGpu ) ) 
		 != DXGI_ERROR_NOT_FOUND;
		 ++i )
	{
		pGpu->GetDesc3( &pGpuInfo );
		HR_CHECK( D3D12CreateDeviceProc( pGpu, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS( &pDevice ) ) );
		D3D12_FEATURE_DATA_SHADER_MODEL smLevel = { D3D_SHADER_MODEL_6_7 };
		HR_CHECK( pDevice->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &smLevel, sizeof( smLevel ) ) );
		D3D12_FEATURE_DATA_D3D12_OPTIONS d12Options = { 
			.ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3, 
			.ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2 };
		HR_CHECK( pDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &d12Options, sizeof( d12Options ) ) );
		// TODO: handle more stuff
		break;
	}
	pGpu->Release();

	ID3D12CommandQueue* pCmdQueue = 0;
	D3D12_COMMAND_QUEUE_DESC queueDesc = { .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
	HR_CHECK( pDevice->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &pCmdQueue ) ) );

	ID3D12Fence1* pFence = 0;
	// TODO: shared fences ?
	HR_CHECK( pDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &pFence ) ) );



#ifdef DX12_DEBUG
	ID3D12InfoQueue* pInfoQueue = 0;
	HR_CHECK( pDevice->QueryInterface( &pInfoQueue ) );
	HR_CHECK( pInfoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, true ) );
	HR_CHECK( pInfoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, true ) );
	HR_CHECK( pInfoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_WARNING, true ) );

	D3D12_MESSAGE_SEVERITY severityFilter[] = { D3D12_MESSAGE_SEVERITY_INFO };

	D3D12_MESSAGE_ID msgFilter[] =
	{
		D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
		D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
		D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
		D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
		D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_DEPTHSTENCILVIEW_NOT_SET
	};

	D3D12_INFO_QUEUE_FILTER filter = {};
	filter.DenyList.NumSeverities = std::size( severityFilter );
	filter.DenyList.pSeverityList = severityFilter;
	filter.DenyList.NumIDs = std::size( msgFilter );
	filter.DenyList.pIDList = msgFilter;
	HR_CHECK( pInfoQueue->PushStorageFilter( &filter ) );

	//DWORD callbackId = 0;
	//HR_CHECK( pInfoQueue->RegisterMessageCallback( Dx12ErrorCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, 0, &callbackId ) );
	//assert( callbackId );
	pInfoQueue->Release();
#endif // DX12_DEBUG

	return{ pDevice,pCmdQueue,pFence };
}

static dx12_device dx12Device;

// TODO: iterate on this  
// TODO: tag and name so we can track
// TODO: add memory checks ?
struct dx12_device_block
{
	ID3D12Heap* mem;
	UINT64 size;
	UINT64 allocated;
};

// TODO: use this
//struct dx12_allocation
//{
//	UINT32 offset;
//	UINT32 size;
//	UINT16 blockId;
//};
// TODO: handle heap, uasge, rsc desc better
// TODO: how to handle CUSTOM HEAPS when time comes ?
struct dx12_mem_arena
{
	std::vector<dx12_device_block> blocks;
	D3D12_HEAP_TYPE heapType;
	D3D12_HEAP_FLAGS usgFlags;
	UINT32 defaultBlockSize;
};

// TODO: improve
// TODO: mem tracking
struct dx12_allocation
{
	ID3D12Heap* mem;
	UINT64 offset;
};

inline bool IsPowOf2( u64 addr )
{
	return !( addr & ( addr - 1 ) );
}
inline u64 FwdAlign( u64 addr, u64 alignment )
{
	assert( IsPowOf2( alignment ) );
	u64 mod = addr & ( alignment - 1 );
	return mod ? addr + ( alignment - mod ) : addr;
}

// TODO: different patterns ?
inline static dx12_allocation Dx12ArenaAlignAlloc( 
	const D3D12_RESOURCE_ALLOCATION_INFO& allocInfo,
	ID3D12Device9* pDevice,
	dx12_mem_arena& rscArena 
){
	HR_CHECK( ( allocInfo.SizeInBytes != UINT64_MAX ) ? S_OK : E_FAIL );

	// TODO: huh ?
	dx12_device_block lastBlock = ( !std::size( rscArena.blocks ) ) ?
		dx12_device_block{} : rscArena.blocks[ std::size( rscArena.blocks ) - 1 ];
	UINT64 alignedAllocated = FwdAlign( lastBlock.allocated, allocInfo.Alignment );
	if( alignedAllocated + allocInfo.SizeInBytes > lastBlock.size )
	{
		HR_CHECK( ( rscArena.heapType != D3D12_HEAP_TYPE_CUSTOM ) ? S_OK : E_FAIL );

		D3D12_HEAP_DESC heapDesc = {};
		heapDesc.SizeInBytes = allocInfo.SizeInBytes;
		heapDesc.Properties.Type = rscArena.heapType;
		heapDesc.Properties.CPUPageProperty = ( rscArena.heapType == D3D12_HEAP_TYPE_UPLOAD ) ?
			D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE : D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Alignment = allocInfo.Alignment;
		heapDesc.Flags = rscArena.usgFlags;

		ID3D12Heap* pHeap = 0;
		HR_CHECK( pDevice->CreateHeap( &heapDesc, IID_PPV_ARGS( &pHeap ) ) );

		rscArena.blocks.push_back( { pHeap, heapDesc.SizeInBytes, 0 } );
	}

	ID3D12Heap* pMem = rscArena.blocks[ std::size( rscArena.blocks ) - 1 ].mem;
	UINT64 offset = rscArena.blocks[ std::size( rscArena.blocks ) - 1 ].allocated;
	rscArena.blocks[ std::size( rscArena.blocks ) - 1 ].allocated = alignedAllocated + allocInfo.SizeInBytes;

	return { pMem,offset };
}

// NOTE: RTs will be alloc-ed separately
// TODO: host visible textures ?
static dx12_mem_arena dx12BufferArena;
static dx12_mem_arena dx12TextureArena;
static dx12_mem_arena dx12HostComArena;


struct dx12_buffer_desc
{
	UINT32 count;
	UINT32 stride;
	D3D12_RESOURCE_FLAGS usgFlags;
	bool hasUavCounter;
};

// TODO: add sample stuff
struct dx12_texture_desc
{
	UINT16 width;
	UINT16 height;
	UINT8 mipCount;
	UINT8 depthOrArrayCount;
	D3D12_RESOURCE_FLAGS usgFlags;
	DXGI_FORMAT format;
};

// TODO: check sizes and stuff
// TODO: allow other uses ?
// TODO: no asserts
inline static D3D12_RESOURCE_DESC Dx12ResouceFromBufferDesc( const dx12_buffer_desc& buffDesc )
{
	assert( buffDesc.usgFlags == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS || buffDesc.usgFlags == D3D12_RESOURCE_FLAG_NONE );

	D3D12_RESOURCE_DESC rscDesc = {};
	rscDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rscDesc.Alignment = 0;
	rscDesc.Width = buffDesc.count * buffDesc.stride;
	rscDesc.Height = rscDesc.DepthOrArraySize = rscDesc.MipLevels = 1;
	rscDesc.Format = DXGI_FORMAT_UNKNOWN;
	rscDesc.SampleDesc = { 1,0 };
	rscDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rscDesc.Flags = buffDesc.usgFlags;

	return rscDesc;
}
inline static D3D12_RESOURCE_DESC Dx12ResouceFromTextureDesc( const dx12_texture_desc& texDesc )
{
	D3D12_RESOURCE_DESC rscDesc = {};
	rscDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rscDesc.Alignment = 0;
	rscDesc.Width = texDesc.width;
	rscDesc.Height = texDesc.height; 
	rscDesc.DepthOrArraySize = 1;
	rscDesc.MipLevels = texDesc.mipCount;
	rscDesc.Format = texDesc.format;
	rscDesc.SampleDesc = { 1,0 };
	rscDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rscDesc.Flags = texDesc.usgFlags;

	return rscDesc;
}

// TODO: store more stuff
// TODO: separate buffers and tx ?
struct dx12_resource
{
	dx12_allocation hAlloc;
	ID3D12Resource2* pRsc;
	UINT32 magicNum;
};

// TODO: more options and parameters ?
// TODO: more resource view types
// TODO: revisit when using MS images and stuff
// TODO: wait for templates to get good in HLSL , until then use ByteAddressBuffers
// TODO: DXGI_FORMAT_R32_TYPELESS for BABs 
inline static std::pair<D3D12_SHADER_RESOURCE_VIEW_DESC, D3D12_UNORDERED_ACCESS_VIEW_DESC>
Dx12MakeResourceViewPairFromBufferDesc( const dx12_buffer_desc& pRscDesc, UINT32 counterOffset )
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32_TYPELESS;
	srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srv.Buffer.FirstElement = 0;
	srv.Buffer.NumElements = pRscDesc.count;
	srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	srv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
		D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
		D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
		D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2,
		D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3
	);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
	uav.Format = DXGI_FORMAT_R32_TYPELESS;
	uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uav.Buffer.FirstElement = 0;
	uav.Buffer.NumElements = pRscDesc.count;
	uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
	uav.Buffer.CounterOffsetInBytes = counterOffset;
	
	return { srv,uav };
}
// TODO: redundancy for mipped tex 
// TODO: mip clamp
// TODO: elem swizzeling ?
inline static std::pair<D3D12_SHADER_RESOURCE_VIEW_DESC, D3D12_UNORDERED_ACCESS_VIEW_DESC>
Dx12MakeResourceViewPairFromTextureDesc( const dx12_texture_desc& rscDesc, UINT32 mipLevel, UINT32 componentsSwizzeling )
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = rscDesc.format;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MostDetailedMip = 0;
	srv.Texture2D.MipLevels = rscDesc.mipCount;
	srv.Texture2D.PlaneSlice = 0;
	srv.Texture2D.ResourceMinLODClamp = rscDesc.mipCount;
	srv.Shader4ComponentMapping = componentsSwizzeling;
	D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
	uav.Format = rscDesc.format;
	uav.Texture2D.MipSlice = mipLevel;
	uav.Texture2D.PlaneSlice = 0;

	return { srv,uav };
}

// IDEA: null descriptors for read/write view that's not supposed to be accessed ?
// TODO: rename ?
struct dx12_descriptor
{
	ID3D12DescriptorHeap* heap;
	D3D12_DESCRIPTOR_HEAP_TYPE descType;
	UINT32 stride;
	UINT32 maxSize;
	UINT32 count;
};

inline static D3D12_CPU_DESCRIPTOR_HANDLE Dx12GetDescHeapPtrFromIdx( const dx12_descriptor& desc, UINT32 idx )
{
	return { desc.heap->GetCPUDescriptorHandleForHeapStart().ptr + idx * desc.stride };
}

static dx12_descriptor dx12RscDescHeap;
static dx12_descriptor dx12SamplerDescHeap;
// TODO: revisit these
static dx12_descriptor dx12DescHeapRenderTargets;
static dx12_descriptor dx12DescHeapDepthSetncil;

// NOTE: ver will wrap around, doesn't matter
struct render_hndl
{
	UINT32 h;

	inline render_hndl() = default;
	inline render_hndl( UINT32 verTagIdx ) : h{ verTagIdx }{}
	inline operator UINT32() const { return h; }
};

// TODO: no assert
inline render_hndl RenderHndlFromVerTagIdx( UINT32 ver, UINT32 tag, UINT32 idx )
{
	assert( ver < 64 );
	assert( tag < 8 );
	assert( idx < ( 1U << 23 ) );

	return { ver << 26 | tag << 23 | idx };
}
inline UINT32 RenderHndlGetIdx( render_hndl h )
{
	constexpr UINT32 standardIndexMask = ( 1U << 23 ) - 1;
	return h & standardIndexMask;
}

struct resource_hndl_pair
{
	render_hndl srv, uav;
};
// TODO: version tag
inline static resource_hndl_pair Dx12AllocateViewPairHandle( dx12_descriptor& desc )
{
	UINT32 thisPairStart = desc.count;
	desc.count += 2;
	return { RenderHndlFromVerTagIdx( 0,0,thisPairStart ), RenderHndlFromVerTagIdx( 0,0,thisPairStart + 1 ) };
}

// TODO: not global
extern HWND hWnd;

inline void Dx12BackendInit()
{
	HMODULE dx12AgilityDll = LoadLibraryA( "D3D12.dll" );
	assert( dx12AgilityDll );

	HMODULE dxgiDll = LoadLibraryA( "dxgi.dll" );
	assert( dxgiDll );


	using PFN_CreateDXGIFactory2 = HRESULT( __stdcall* )( _In_ UINT,_In_ REFIID, _Out_ LPVOID* );

	PFN_CreateDXGIFactory2 CreateDXGIFactoryProc2 = ( PFN_CreateDXGIFactory2 ) GetProcAddress( dxgiDll, "CreateDXGIFactory2" );

	D3D12CreateDeviceProc = ( PFN_D3D12_CREATE_DEVICE ) GetProcAddress( dx12AgilityDll, "D3D12CreateDevice" );


	DWORD dxgiFactoryFlags = 0;

#ifdef DX12_DEBUG

#pragma comment(lib, "dxguid.lib")

	using PFN_DXGIGetDebugInterface1 = HRESULT( WINAPI* )( UINT, REFIID, _COM_Outptr_ void** );

	PFN_DXGIGetDebugInterface1 DXGIGetDebugInterface1Proc = 
		( PFN_DXGIGetDebugInterface1 ) GetProcAddress( dxgiDll, "DXGIGetDebugInterface1" );

	PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterfaceProc =
		( PFN_D3D12_GET_DEBUG_INTERFACE ) GetProcAddress( dx12AgilityDll, "D3D12GetDebugInterface" );


	dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

	ID3D12Debug5* pDebugController = 0;
	HR_CHECK( D3D12GetDebugInterfaceProc( IID_PPV_ARGS( &pDebugController ) ) );
	pDebugController->EnableDebugLayer();
	pDebugController->Release();

	IDXGIInfoQueue* dxgiInfoQueue = 0;
	HR_CHECK( DXGIGetDebugInterface1Proc( 0, IID_PPV_ARGS( &dxgiInfoQueue ) ) );
	dxgiInfoQueue->SetBreakOnSeverity( DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true );
	dxgiInfoQueue->SetBreakOnSeverity( DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true );
	dxgiInfoQueue->Release();
#endif


	IDXGIFactory7* pDxgiFactory = 0;
	HR_CHECK( CreateDXGIFactoryProc2( dxgiFactoryFlags, IID_PPV_ARGS( &pDxgiFactory ) ) );


	dx12Device = Dx12CreateDeviceContext( pDxgiFactory );

	dx12BufferArena = {
		.heapType = D3D12_HEAP_TYPE_DEFAULT,
		.usgFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS,
		.defaultBlockSize = 256 * MB
	};
	dx12TextureArena = {
		.heapType = D3D12_HEAP_TYPE_DEFAULT,
		.usgFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS,
		.defaultBlockSize = 256 * MB
	};
	dx12HostComArena = {
		.heapType = D3D12_HEAP_TYPE_UPLOAD,
		.usgFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
		.defaultBlockSize = 256 * MB
	};


	dx12RscDescHeap = {
		.descType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.stride = dx12Device.pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ),
		.maxSize = UINT16_MAX,
		.count = 0
	};
	
	D3D12_DESCRIPTOR_HEAP_DESC descHeapInfo = { 
		.Type = dx12RscDescHeap.descType, 
		.NumDescriptors = dx12RscDescHeap.maxSize, 
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE 
	};
	HR_CHECK( dx12Device.pDevice->CreateDescriptorHeap( &descHeapInfo, IID_PPV_ARGS( &dx12RscDescHeap.heap ) ) );

	ID3D12Device9* pDevice = dx12Device.pDevice;

	dx12_buffer_desc buffDesc = { 1000,4 * sizeof( float ),D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, false };

	D3D12_RESOURCE_DESC rscDesc = Dx12ResouceFromBufferDesc( buffDesc );
	dx12_allocation alloc = Dx12ArenaAlignAlloc( pDevice->GetResourceAllocationInfo( 0, 1, &rscDesc ), pDevice, dx12BufferArena );
	ID3D12Resource* pRsc = 0;
	HR_CHECK( pDevice->CreatePlacedResource( alloc.mem, alloc.offset, &rscDesc, D3D12_RESOURCE_STATE_COMMON, 0, IID_PPV_ARGS( &pRsc ) ) );


	resource_hndl_pair hRscPair = Dx12AllocateViewPairHandle( dx12RscDescHeap );

	auto viewDescPair = Dx12MakeResourceViewPairFromBufferDesc( buffDesc, 0 );
	pDevice->CreateShaderResourceView(
		pRsc, &viewDescPair.first, Dx12GetDescHeapPtrFromIdx( dx12RscDescHeap, RenderHndlGetIdx( hRscPair.srv ) ) );
	pDevice->CreateUnorderedAccessView( 
		pRsc, 0, &viewDescPair.second, Dx12GetDescHeapPtrFromIdx( dx12RscDescHeap, RenderHndlGetIdx( hRscPair.uav ) ) );


	struct dx12_swapchain
	{
		IDXGISwapChain1* sc;
		DXGI_FORMAT format;
		DXGI_USAGE usg;
		UINT16 width;
		UINT16 height;
		UINT8 bufferCount;
	};

	//DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	// TODO: can't comptue write to sc, this sucks
	DXGI_USAGE usg = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	dx12_swapchain sc = {};

	DXGI_SWAP_CHAIN_DESC1 scInfo = {};
	scInfo.Width = sc.width = SCREEN_WIDTH;
	scInfo.Height = sc.height = SCREEN_HEIGHT;
	scInfo.Format = sc.format = format;
	scInfo.Stereo = false;
	scInfo.SampleDesc = { 1,0 };
	scInfo.BufferUsage = sc.usg = usg;
	scInfo.BufferCount = sc.bufferCount = 3;
	scInfo.Scaling = DXGI_SCALING_NONE;
	scInfo.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	scInfo.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

	IDXGISwapChain1* scOut = 0;
	HR_CHECK( pDxgiFactory->CreateSwapChainForHwnd( dx12Device.pCmdQueue, hWnd, &scInfo, 0, 0, &scOut ) );
}