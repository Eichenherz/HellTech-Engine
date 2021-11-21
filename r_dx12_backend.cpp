#define DX12_DEBUG

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

// TODO: don't really need this
#include <vector>
#include "sys_os_api.h"


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

// NOTE: it's a full assert
// TODO: custom err code based on HRESULT
#define HR_ASSERT( x ) if( !x ) HR_CHECK( E_FAIL )

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




constexpr UINT64 maxDx12FramesInFlight = 2;
constexpr BOOL asyncComputeQueue = TRUE;




// TODO: not global ?
PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceProc;

// TODO: better queue + fence abstraction
enum dx12_queue_id : UINT8
{
	DX12_QUEUE_ID_DIRECT,
	DX12_QUEUE_ID_COMPUTE,
	DX12_QUEUE_ID_COUNT
};

struct dx12_device
{
	ID3D12Device9* pDevice;
	ID3D12CommandQueue* pCmdQueues[ DX12_QUEUE_ID_COUNT ];
	ID3D12Fence1* pFences[ DX12_QUEUE_ID_COUNT ];
	UINT64 fenceCounters[ DX12_QUEUE_ID_COUNT ];
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

		D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignatureVer = { D3D_ROOT_SIGNATURE_VERSION_1_1 };
		HR_CHECK( pDevice->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &rootSignatureVer, sizeof( rootSignatureVer ) ) );

		D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
		HR_CHECK( pDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof( options3 ) ) );

		D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
		HR_CHECK( pDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof( options7 ) ) );

		// TODO: handle more stuff
		break;
	}
	pGpu->Release();

	ID3D12CommandQueue* pCmdQueues[ DX12_QUEUE_ID_COUNT ];
	D3D12_COMMAND_QUEUE_DESC directDueueDesc = { .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
	HR_CHECK( pDevice->CreateCommandQueue( &directDueueDesc, IID_PPV_ARGS( &pCmdQueues[ 0 ] ) ) );

	D3D12_COMMAND_QUEUE_DESC computeQueueDesc = { .Type = D3D12_COMMAND_LIST_TYPE_COMPUTE };
	HR_CHECK( pDevice->CreateCommandQueue( &computeQueueDesc, IID_PPV_ARGS( &pCmdQueues[ 1 ] ) ) );

	ID3D12Fence1* pFences[ DX12_QUEUE_ID_COUNT ];
	HR_CHECK( pDevice->CreateFence( 0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS( &pFences[ 0 ] ) ) );
	HR_CHECK( pDevice->CreateFence( 0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS( &pFences[ 1 ] ) ) );



#ifdef DX12_DEBUG
	ID3D12InfoQueue* pInfoQueue;
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

	dx12_device dc = {};
	dc.pDevice = pDevice;
	dc.pCmdQueues[ 0 ] = pCmdQueues[ 0 ];
	dc.pCmdQueues[ 1 ] = pCmdQueues[ 1 ];
	dc.pFences[ 0 ] = pFences[ 0 ];
	dc.pFences[ 1 ] = pFences[ 1 ];

	return dc;
}

// TODO: iterate on this  
// TODO: tag and name so we can track
// TODO: add memory checks ?
struct dx12_device_block
{
	ID3D12Heap* mem;
	UINT64 size;
	UINT64 allocated;
};


// TODO: handle heap, uasge, rsc desc better
// TODO: how to handle CUSTOM HEAPS when time comes ?
struct dx12_mem_arena
{
	std::vector<dx12_device_block> blocks;
	D3D12_HEAP_TYPE heapType;
	D3D12_HEAP_FLAGS usgFlags;
	UINT32 defaultBlockSize;
};

// TODO: alloc handle depends on alloc pattern
// TODO: improve
// TODO: mem tracking
struct dx12_allocation
{
	ID3D12Heap* mem;
	UINT64 offset;
};

// TODO: use this
//struct dx12_allocation
//{
//	UINT32 offset;
//	UINT32 size;
//	UINT16 blockId;
//};

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

// TODO: revisit
inline static auto Dx12DescGetNextHandleAndIndex( dx12_descriptor& desc )
{
	struct __retval
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle;
		UINT32 idx;
	};
	UINT32 currentCount = desc.count++;
	return __retval{ desc.heap->GetCPUDescriptorHandleForHeapStart().ptr + currentCount * desc.stride,currentCount };
}

// TODO: typed ?
struct hndl32
{
	u32 h;

	inline hndl32() = default;
	inline hndl32( u32 magicIdx ) : h{ magicIdx }{}
	inline operator u32() const { return h; }
};

inline u32 IdxFromHndl32( hndl32 h )
{
	constexpr u32 standardIndexMask = ( 1ull << sizeof( hndl32 ) * 4 ) - 1;
	return h & standardIndexMask;
}
inline u32 MagicFromHndl32( hndl32 h )
{
	constexpr u32 standardIndexMask = ( 1ull << sizeof( hndl32 ) * 4 ) - 1;
	constexpr u32 standardMagicNumberMask = ~standardIndexMask;
	return ( h & standardMagicNumberMask ) >> sizeof( hndl32 ) * 4;
}
inline hndl32 Hndl32FromMagicAndIdx( u32 m, u32 i )
{
	return u32( ( m << sizeof( hndl32 ) * 4 ) | i );
}

// TODO: rethink
// TODO: add validation ?
struct view_handle
{
	using srv = UINT16;
	using uav = UINT16;

	UINT16 h;

	inline view_handle() = default;
	inline view_handle( UINT16 idx ) : h{ idx }{}
	inline operator UINT16() const { return h; }
	//inline operator srv() const { return h; }
	//inline operator uav() const { return h + 1; }
};


// TODO: add views info as UINT8 ?
// TODO: better ?
// TODO: special allocator for : dx12_resource | viewIndices | views
// TODO: special vector container ?
// TODO: store more stuff
struct dx12_buffer
{
	dx12_allocation hAlloc;
	ID3D12Resource2* pRsc;
	UINT8* hostVisible;
	UINT32 magicNum;
	view_handle hView;
};

struct dx12_texture
{
	dx12_allocation hAlloc;
	ID3D12Resource2* pRsc;
	UINT32 magicNum;
	view_handle hView;
	view_handle optionalViews[ 16 ];
	UINT8 mipCount;
	UINT8 layerCount;
};

// TODO: no assert
inline view_handle Dx12GetTextureView( const dx12_texture& tex, UINT8 mip, UINT8 layer )
{
	assert( layer < tex.layerCount && mip < tex.mipCount );
	return tex.optionalViews[ layer * tex.layerCount + mip ];
}

// TODO: better automate these
struct dx12_buffer_desc
{
	UINT32 count;
	UINT32 stride;
	D3D12_RESOURCE_FLAGS usgFlags;
	UINT8 isRawBuffer : 1;
	UINT8 isHostVisible : 1;
};

// TODO: add component swizzle
// TODO: add sample stuff
struct dx12_texture_desc
{
	UINT16 width;
	UINT16 height;
	UINT8 mipCount;
	UINT8 layerCount;
	D3D12_RESOURCE_FLAGS usgFlags;
	DXGI_FORMAT format;
};

#define DX12_DEFAULT_COMPONENT_MAPPING  \
D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING( \
D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, \
D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1, \
D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2, \
D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3 )

// TODO: null descriptor for read/write depending on the use case
// TODO: check alignment
// TODO: no assert
// TODO: buff resource view type
// TODO: handle handles
inline static hndl32 Dx12CreateBuffer( 
	const dx12_buffer_desc& buffDesc,
	ID3D12Device9* pDevice,
	dx12_mem_arena& dx12MemArena,
	dx12_descriptor& dx12RscDescHeap,
	std::vector<dx12_buffer>& resourceContainer
){
	D3D12_RESOURCE_DESC rscDesc = {};
	rscDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rscDesc.Alignment = 0;
	rscDesc.Width = buffDesc.count * buffDesc.stride;
	rscDesc.Height = rscDesc.DepthOrArraySize = rscDesc.MipLevels = 1;
	rscDesc.Format = DXGI_FORMAT_UNKNOWN;
	rscDesc.SampleDesc = { 1,0 };
	rscDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rscDesc.Flags = buffDesc.usgFlags;

	dx12_allocation alloc = Dx12ArenaAlignAlloc( pDevice->GetResourceAllocationInfo( 0, 1, &rscDesc ), pDevice, dx12MemArena );

	ID3D12Resource2* pRsc = 0;
	HR_CHECK( pDevice->CreatePlacedResource( 
		alloc.mem, alloc.offset, &rscDesc, D3D12_RESOURCE_STATE_COMMON, 0, IID_PPV_ARGS( &pRsc ) ) );
	UINT8* hostVisible = 0;
	if( buffDesc.isHostVisible ) HR_CHECK( pRsc->Map( 0, 0, ( void** ) &hostVisible ) );

	DXGI_FORMAT viewFormat = buffDesc.isRawBuffer ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvInfo = {};
	srvInfo.Format = viewFormat;
	srvInfo.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvInfo.Buffer.FirstElement = 0;
	srvInfo.Buffer.NumElements = buffDesc.count;
	srvInfo.Buffer.StructureByteStride = buffDesc.isRawBuffer ? 0 : buffDesc.stride;
	srvInfo.Buffer.Flags = buffDesc.isRawBuffer ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
	srvInfo.Shader4ComponentMapping = DX12_DEFAULT_COMPONENT_MAPPING;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavInfo = {};
	uavInfo.Format = viewFormat;
	uavInfo.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavInfo.Buffer.FirstElement = 0;
	uavInfo.Buffer.NumElements = buffDesc.count;
	uavInfo.Buffer.StructureByteStride = buffDesc.isRawBuffer ? 0 : buffDesc.stride;
	uavInfo.Buffer.Flags = buffDesc.isRawBuffer ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
	
	auto[ srvHandle, srvIndex ] = Dx12DescGetNextHandleAndIndex( dx12RscDescHeap );
	pDevice->CreateShaderResourceView( pRsc, &srvInfo, srvHandle );
	auto[ uavHandle, uavIndex ] = Dx12DescGetNextHandleAndIndex( dx12RscDescHeap );
	pDevice->CreateUnorderedAccessView( pRsc, 0, &uavInfo, uavHandle );


	dx12_buffer rsc = {};
	rsc.hAlloc = alloc;
	rsc.pRsc = pRsc;
	rsc.hostVisible = hostVisible;
	rsc.hView = srvIndex;
	resourceContainer.push_back( rsc );

	return Hndl32FromMagicAndIdx( 0, std::size( resourceContainer ) - 1 );
}


inline static hndl32 Dx12CreateTexture(
	const dx12_texture_desc& texDesc,
	ID3D12Device9* pDevice,
	dx12_mem_arena& dx12MemArena,
	dx12_descriptor& dx12RscDescHeap,
	std::vector<dx12_texture>& resourceContainer
){
	D3D12_RESOURCE_DESC rscDesc = {};
	rscDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rscDesc.Alignment = 0;
	rscDesc.Width = texDesc.width;
	rscDesc.Height = texDesc.height;
	rscDesc.DepthOrArraySize = texDesc.layerCount;
	rscDesc.MipLevels = texDesc.mipCount;
	rscDesc.Format = texDesc.format;
	rscDesc.SampleDesc = { 1,0 };
	rscDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rscDesc.Flags = texDesc.usgFlags;

	dx12_allocation alloc = Dx12ArenaAlignAlloc( pDevice->GetResourceAllocationInfo( 0, 1, &rscDesc ), pDevice, dx12MemArena );

	ID3D12Resource2* pRsc = 0;
	HR_CHECK( pDevice->CreatePlacedResource(
		alloc.mem, alloc.offset, &rscDesc, D3D12_RESOURCE_STATE_COMMON, 0, IID_PPV_ARGS( &pRsc ) ) );

	
	D3D12_SHADER_RESOURCE_VIEW_DESC srvInfo = {};
	srvInfo.Format = texDesc.format;
	srvInfo.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvInfo.Texture2D.MostDetailedMip = 0;
	srvInfo.Texture2D.MipLevels = texDesc.mipCount;
	srvInfo.Texture2D.PlaneSlice = 0;
	srvInfo.Texture2D.ResourceMinLODClamp = texDesc.mipCount;
	srvInfo.Shader4ComponentMapping = DX12_DEFAULT_COMPONENT_MAPPING;
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavInfo = {};
	uavInfo.Format = texDesc.format;
	uavInfo.Texture2D.MipSlice = 0;
	uavInfo.Texture2D.PlaneSlice = 0;

	auto [srvHandle, srvIndex] = Dx12DescGetNextHandleAndIndex( dx12RscDescHeap );
	pDevice->CreateShaderResourceView( pRsc, &srvInfo, srvHandle );
	auto [uavHandle, uavIndex] = Dx12DescGetNextHandleAndIndex( dx12RscDescHeap );
	pDevice->CreateUnorderedAccessView( pRsc, 0, &uavInfo, uavHandle );


	dx12_texture tex = {};
	tex.hAlloc = alloc;
	tex.pRsc = pRsc;
	tex.hView = srvIndex;
	tex.mipCount = rscDesc.MipLevels;
	tex.layerCount = rscDesc.DepthOrArraySize;

	assert( tex.layerCount == 1 );
	for( UINT64 mi = 0; mi < rscDesc.MipLevels; ++mi )
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvInfo = {};
		srvInfo.Format = texDesc.format;
		srvInfo.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvInfo.Texture2D.MostDetailedMip = mi;
		srvInfo.Texture2D.MipLevels = 1;
		srvInfo.Texture2D.PlaneSlice = 0;
		srvInfo.Texture2D.ResourceMinLODClamp = texDesc.mipCount;
		srvInfo.Shader4ComponentMapping = DX12_DEFAULT_COMPONENT_MAPPING;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavInfo = {};
		uavInfo.Format = texDesc.format;
		uavInfo.Texture2D.MipSlice = mi;
		uavInfo.Texture2D.PlaneSlice = 0;

		auto [srvHandle, srvIndex] = Dx12DescGetNextHandleAndIndex( dx12RscDescHeap );
		pDevice->CreateShaderResourceView( pRsc, &srvInfo, srvHandle );
		auto [uavHandle, uavIndex] = Dx12DescGetNextHandleAndIndex( dx12RscDescHeap );
		pDevice->CreateUnorderedAccessView( pRsc, 0, &uavInfo, uavHandle );

		tex.optionalViews[ mi ] = srvIndex;
	}
	
	resourceContainer.push_back( tex );

	return Hndl32FromMagicAndIdx( 0, std::size( resourceContainer ) - 1 );
}

// TODO: extra checks
inline static auto Dx12CreateUploadBuffer(
	UINT32 size,
	ID3D12Device9* pDevice,
	dx12_mem_arena& hostComArena
){
	D3D12_RESOURCE_DESC rscDesc = {};
	rscDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rscDesc.Alignment = 0;
	rscDesc.Width = size;
	rscDesc.Height = rscDesc.DepthOrArraySize = rscDesc.MipLevels = 1;
	rscDesc.Format = DXGI_FORMAT_UNKNOWN;
	rscDesc.SampleDesc = { 1,0 };
	rscDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rscDesc.Flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

	dx12_allocation alloc = Dx12ArenaAlignAlloc( pDevice->GetResourceAllocationInfo( 0, 1, &rscDesc ), pDevice, hostComArena );

	ID3D12Resource2* pRsc = 0;
	constexpr D3D12_RESOURCE_STATES rscInitState = D3D12_RESOURCE_STATE_COMMON;
	HR_CHECK( pDevice->CreatePlacedResource( alloc.mem, alloc.offset, &rscDesc, rscInitState, 0, IID_PPV_ARGS( &pRsc ) ) );
	UINT8* hostVisible = 0;
	HR_CHECK( pRsc->Map( 0, 0, ( void** ) &hostVisible ) );

	struct __retval
	{
		dx12_allocation hAlloc;
		ID3D12Resource2* pRsc;
		UINT8* hostVisible;
	};

	return __retval{ alloc,pRsc,hostVisible };
}

#include <dxcapi.h>
// TODO: write own ShaderBlob to control memory ?
// NOTE: massive help : https://simoncoenen.com/blog/programming/graphics/DxcCompiling + his github
struct shader_file_keyval 
{
	UINT64 key; 
	UINT64 timestamp;  
	IDxcBlobEncoding* pEncoding; 
};
// NOTE: must set additional dll path for dxil.dll
// TODO: add path programatically ?
struct dxc_context
{
	IDxcCompiler3* pCompiler;
	IDxcUtils* pUtils;
	IDxcValidator* pValidator;
	IDxcIncludeHandler* pIncludeHandler;

	std::vector<shader_file_keyval> includeHashmap;
};


// TODO: check stuff
// TODO: no assert
inline dxc_context DxcCreateContext()
{
	assert( SetDllDirectoryA("C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.22000.0\\x64"));
	HMODULE dll = LoadLibraryA( "dxcompiler.dll" );
	assert( dll );
	DxcCreateInstanceProc DxcMakeInstanceProc = ( DxcCreateInstanceProc ) GetProcAddress( dll, "DxcCreateInstance" );
	
	dxc_context ctx = {};

	HR_CHECK( DxcMakeInstanceProc( CLSID_DxcCompiler, IID_PPV_ARGS( &ctx.pCompiler ) ) );
	HR_CHECK( DxcMakeInstanceProc( CLSID_DxcUtils, IID_PPV_ARGS( &ctx.pUtils ) ) );
	HR_CHECK( DxcMakeInstanceProc( CLSID_DxcValidator, IID_PPV_ARGS( &ctx.pValidator ) ) );

	HR_CHECK( ctx.pUtils->CreateDefaultIncludeHandler( &ctx.pIncludeHandler ) );

	return ctx;
}


// TODO: own string stuff
// NOTE: from http://0x80.pl/articles/simd-strfind.html
#include <immintrin.h>
UINT64 FindSubstringAvx( const char* str, UINT64 strSize, const char* needle, UINT64 needleSize )
{
	const __m256i first = _mm256_set1_epi8( needle[ 0 ] );
	const __m256i last = _mm256_set1_epi8( needle[ needleSize - 1 ] );

	for( UINT64 i = 0; i < strSize; i += 32 )
	{
		const __m256i blockFirst = _mm256_loadu_si256( ( const __m256i* )( str + i ) );
		const __m256i blockLast = _mm256_loadu_si256( ( const __m256i* )( str + i + needleSize - 1 ) );

		const __m256i eqFirst = _mm256_cmpeq_epi8( first, blockFirst );
		const __m256i eqLast = _mm256_cmpeq_epi8( last, blockLast );

		UINT mask = _mm256_movemask_epi8( _mm256_and_si256( eqFirst, eqLast ) );

		while( mask != 0 )
		{
			UINT32 bitPos = _tzcnt_u32( mask );

			if( std::memcmp( str + i + bitPos + 1, needle + 1, needleSize - 2 ) == 0 )
			{
				return i + bitPos;
			}

			mask = mask & ( mask - 1 );
		}
	}

	return -1;
}

// TODO: revisit
// TODO: hot reloading
inline auto DxcLoadHeaderBlob( 
	LPCWSTR shaderFilename, 
	IDxcUtils* pUtils,
	std::vector<shader_file_keyval>& fileHashmap
){
	struct __retval
	{
		IDxcBlobEncoding* pEncoding;
		HRESULT hr;
	};

	constexpr LPCWSTR pValidExtensions[] = { L"hlsli", L"h" };

	std::wstring_view pathView = { shaderFilename };

	std::wstring_view extView = { shaderFilename + pathView.find_last_of( L"." ) + 1 };
	if( extView != pValidExtensions[0] && extView != pValidExtensions[1] )
	{
		std::cout << "Include path does not have a valid extension." << '\n';
		return __retval{ 0, E_FAIL };
	}

	// TODO: find better way
	char multibyteFilename[ 256 ] = {};
	assert( std::wcslen( shaderFilename ) <= std::size( multibyteFilename ) );
	std::wcstombs( multibyteFilename, shaderFilename, std::wcslen( shaderFilename ) );

	UINT64 thisIncludeHash = std::hash<std::wstring_view>{}( pathView );
	UINT64 thisTimeStamp = SysGetFileTimestamp( multibyteFilename );
	for( auto& keyTimeVal : fileHashmap )
	{
		if( keyTimeVal.key == thisIncludeHash && 
			keyTimeVal.timestamp == thisTimeStamp ) return __retval{ keyTimeVal.pEncoding, S_OK };
	}

	std::vector<u8> hlslIncludeBlob = SysReadFile( multibyteFilename );

	if( std::size( hlslIncludeBlob ) == 0 )
	{
		std::cout << "File can't be opened. Wrong path or doesn't exist." << '\n';
		return __retval{ 0, E_FAIL };
	}

	IDxcBlobEncoding* pEncoding;
	HR_CHECK( pUtils->CreateBlob( std::data( hlslIncludeBlob ), std::size( hlslIncludeBlob ), DXC_CP_UTF8, &pEncoding ) );
	fileHashmap.push_back( { thisIncludeHash, thisTimeStamp, pEncoding } );
	
	return __retval{ pEncoding, S_OK };
}

// TODO: release memory
// TODO: revisit
// TODO: hot reloading
// TODO: better spirv toggle ?
inline IDxcBlob* DxcCompileShader(
	const std::vector<u8>& hlslBlob,
	LPCWSTR* compileOptions,
	UINT64 compileOptionsCount,
	bool compileToSpirv,
	dxc_context& ctx
){
	// NOTE: OOPs interface
	struct CustomIncludeHandler : public IDxcIncludeHandler
	{
		dxc_context& ctx;

		inline CustomIncludeHandler( dxc_context& _ctx ) : ctx{ _ctx }{}

		HRESULT STDMETHODCALLTYPE
		LoadSource( _In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource ) override
		{
			auto[ pEncodingBlob, hr ] = DxcLoadHeaderBlob( pFilename, ctx.pUtils, ctx.includeHashmap );
			*ppIncludeSource = pEncodingBlob;
			return hr;
		}

		HRESULT STDMETHODCALLTYPE
		QueryInterface( REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject ) override
		{
			return ctx.pIncludeHandler->QueryInterface( riid, ppvObject );
		}

		ULONG STDMETHODCALLTYPE AddRef() override { return 0; }
		ULONG STDMETHODCALLTYPE Release() override { return 0; }
	};

	DxcBuffer hlslFileDesc = { std::data( hlslBlob ), std::size( hlslBlob ) };

	CustomIncludeHandler includeHandler = { ctx };

	IDxcResult* pCompileResult;
	HR_CHECK( ctx.pCompiler->Compile(
		&hlslFileDesc, compileOptions, compileOptionsCount, &includeHandler, IID_PPV_ARGS( &pCompileResult ) ) );

	IDxcBlobUtf8* pErrors;
	HR_CHECK( pCompileResult->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( &pErrors ), 0 ) );
	if( pErrors && pErrors->GetStringLength() > 0 )
	{
		const char* errStr = ( char* ) pErrors->GetBufferPointer();
		UINT64 errStrSize = pErrors->GetBufferSize();
		constexpr char needle[] = { "error:" };

		std::cout << "DXC Compile Error: \n" << ( char* ) pErrors->GetBufferPointer() << '\n';

		if( FindSubstringAvx( errStr, errStrSize, needle, std::size( needle ) - 1 ) != UINT64( -1 ) )
		{
			assert( 0 );
			return {};
		}
	}

	IDxcBlob* pShaderBlob;
	HR_CHECK( pCompileResult->GetOutput( DXC_OUT_OBJECT, IID_PPV_ARGS( &pShaderBlob ), 0 ) );

	if( !compileToSpirv )
	{
		IDxcOperationResult* pResult;
		HR_CHECK( ctx.pValidator->Validate( ( IDxcBlob* ) pShaderBlob, DxcValidatorFlags_InPlaceEdit, &pResult ) );
		HRESULT validationResult;
		HR_CHECK( pResult->GetStatus( &validationResult ) );
		if( validationResult != S_OK )
		{
			IDxcBlobEncoding* pPrintBlob;
			IDxcBlobUtf8* pPrintBlobUtf8;
			HR_CHECK( pResult->GetErrorBuffer( &pPrintBlob ) );
			HR_CHECK( ctx.pUtils->GetBlobAsUtf8( pPrintBlob, &pPrintBlobUtf8 ) );

			std::cout << "DXC Validation Error: " << ( char* ) pPrintBlobUtf8->GetBufferPointer() << '\n';
			assert( 0 );
			return{};
		}
	}

	return pShaderBlob;
}

// NOTE: don't care about alignment
struct dx12_pso_config
{
	BOOL blend;
	D3D12_BLEND    srcBlend;
	D3D12_BLEND    dstBlend;
	D3D12_BLEND    srcAlphaBlend;
	D3D12_BLEND    dstAlphaBlend;

	D3D12_FILL_MODE fillMode;
	D3D12_CULL_MODE cullMode;
	BOOL counterClockwiseFrontface;
	D3D12_CONSERVATIVE_RASTERIZATION_MODE conservativeRaster;

	BOOL depthEnable;

	D3D12_PRIMITIVE_TOPOLOGY_TYPE topology;
};

// TODO: handle more render targets ?
ID3D12PipelineState* Dx12MakeGraphicsPso( 
	ID3D12Device9* pDevice, 
	ID3D12RootSignature* pRootSignature,
	IDxcBlob* vsBytecode,
	IDxcBlob* psBytecode,
	DXGI_FORMAT renderTargetFormat,
	DXGI_FORMAT depthStencilFormat,
	const dx12_pso_config& psoConfig
){
	D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendInfo = {};
	renderTargetBlendInfo.BlendEnable = psoConfig.blend;
	renderTargetBlendInfo.SrcBlend = psoConfig.srcBlend;
	renderTargetBlendInfo.DestBlend = psoConfig.dstBlend;
	renderTargetBlendInfo.BlendOp = D3D12_BLEND_OP_ADD;
	renderTargetBlendInfo.SrcBlendAlpha = psoConfig.srcAlphaBlend;
	renderTargetBlendInfo.DestBlendAlpha = psoConfig.dstAlphaBlend;
	renderTargetBlendInfo.BlendOpAlpha = D3D12_BLEND_OP_ADD;

	D3D12_RASTERIZER_DESC rasterizerInfo = {};
	rasterizerInfo.FillMode = psoConfig.fillMode;
	rasterizerInfo.CullMode = psoConfig.cullMode;
	rasterizerInfo.FrontCounterClockwise = psoConfig.counterClockwiseFrontface;
	rasterizerInfo.ConservativeRaster = psoConfig.conservativeRaster;
	
	D3D12_DEPTH_STENCIL_DESC depthStencilInfo = {};
	depthStencilInfo.DepthEnable = psoConfig.depthEnable;
	depthStencilInfo.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoInfo = {};
	psoInfo.pRootSignature = pRootSignature;
	psoInfo.VS = { vsBytecode->GetBufferPointer(),vsBytecode->GetBufferSize() };
	psoInfo.PS = { psBytecode->GetBufferPointer(),psBytecode->GetBufferSize() };
	D3D12_BLEND_DESC blendInfo = {};
	blendInfo.RenderTarget[ 0 ] = renderTargetBlendInfo;
	psoInfo.BlendState = blendInfo;
	psoInfo.RasterizerState = rasterizerInfo;
	psoInfo.DepthStencilState = depthStencilInfo;
	psoInfo.PrimitiveTopologyType = psoConfig.topology;
	psoInfo.NumRenderTargets = 1;
	psoInfo.RTVFormats[ 0 ] = renderTargetFormat;
	psoInfo.DSVFormat = depthStencilFormat;
	psoInfo.SampleDesc = { 1,0 };
	
	ID3D12PipelineState* pso;
	HR_CHECK( pDevice->CreateGraphicsPipelineState( &psoInfo, IID_PPV_ARGS( &pso ) ) );
}


struct virtual_frame
{
	ID3D12CommandAllocator* pCmdAllocators[ DX12_QUEUE_ID_COUNT ];
	ID3D12GraphicsCommandList1* pCmdLists[ DX12_QUEUE_ID_COUNT ];
};

inline static virtual_frame Dx12MakeVirtualFrame( ID3D12Device9* pDevice )
{
	ID3D12CommandAllocator* pCmdAllocators[ DX12_QUEUE_ID_COUNT ];
	HR_CHECK( pDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &pCmdAllocators[ 0 ] ) ) );
	HR_CHECK( pDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS( &pCmdAllocators[ 1 ] ) ) );

	ID3D12GraphicsCommandList1* pCmdLists[ DX12_QUEUE_ID_COUNT ];
	HR_CHECK( pDevice->CreateCommandList( 
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCmdAllocators[ 0 ], 0, IID_PPV_ARGS( &pCmdLists[ 0 ] ) ) );
	HR_CHECK( pDevice->CreateCommandList( 
		0, D3D12_COMMAND_LIST_TYPE_COMPUTE, pCmdAllocators[ 1 ], 0, IID_PPV_ARGS( &pCmdLists[ 1 ] ) ) );


	virtual_frame vf = {};
	vf.pCmdAllocators[ 0 ] = pCmdAllocators[ 0 ];
	vf.pCmdAllocators[ 1 ] = pCmdAllocators[ 1 ];
	vf.pCmdLists[ 0 ] = pCmdLists[ 0 ];
	vf.pCmdLists[ 1 ] = pCmdLists[ 1 ];
	return vf;
}

// TODO: revisit when compute writable swapchain is supported
struct dx12_swapchain
{
	IDXGISwapChain1* sc;
	DXGI_FORMAT format;
	UINT16 width;
	UINT16 height;
	UINT8 bufferCount;
};

inline static dx12_swapchain Dx12MakeSwapchain( 
	IDXGIFactory7* pDxgiFactory, 
	ID3D12CommandQueue* pGfxCmdQueue, 
	HWND hwnd,
	DXGI_FORMAT scFormat,
	UINT32 scWidth,
	UINT32 scHeight,
	UINT32 scBufferCount
){
	DXGI_SWAP_CHAIN_DESC1 scInfo = {};
	scInfo.Width = scWidth;
	scInfo.Height = scHeight;
	scInfo.Format = scFormat;
	scInfo.Stereo = false;
	scInfo.SampleDesc = { 1,0 };
	scInfo.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scInfo.BufferCount = scBufferCount;
	scInfo.Scaling = DXGI_SCALING_NONE;
	scInfo.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	scInfo.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

	IDXGISwapChain1* sc;
	HR_CHECK( pDxgiFactory->CreateSwapChainForHwnd( pGfxCmdQueue, hwnd, &scInfo, 0, 0, &sc ) );

	return { 
		.sc = sc, 
		.format = scInfo.Format, 
		.width = (UINT16)scInfo.Width, 
		.height = (UINT16) scInfo.Height, 
		.bufferCount = (UINT8) scInfo.BufferCount };
}

// TODO: join memory arenas ?
// TODO: join descriptors ?
// TODO: resource manager ?
// TODO: revisit
static struct {
	dx12_device device;

	dx12_swapchain swapchain;

	virtual_frame virtualFrames[ maxDx12FramesInFlight ];

	dx12_mem_arena memArenaBuffers;
	dx12_mem_arena memArenaTextures;
	dx12_mem_arena memArenaHostCom;
	dx12_descriptor descHeapResources;
	dx12_descriptor descHeapSamplers;
	// TODO: revisit these
	dx12_descriptor descHeapRenderTargets;
	dx12_descriptor descHeapDepthSetncil;

	ID3D12RootSignature* globalRootSignature;
} dx12Backend;


extern HWND hWnd;
inline void Dx12BackendInit(  )
{
	HMODULE dx12AgilityDll = LoadLibraryA( "D3D12.dll" );
	assert( dx12AgilityDll );

	HMODULE dxgiDll = LoadLibraryA( "dxgi.dll" );
	assert( dxgiDll );


	using PFN_CreateDXGIFactory2 = HRESULT( __stdcall* )( _In_ UINT, _In_ REFIID, _Out_ LPVOID* );

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


	dx12Backend.device = Dx12CreateDeviceContext( pDxgiFactory );
	ID3D12Device9* pDevice = dx12Backend.device.pDevice;
	ID3D12CommandQueue* pDirectQueue = dx12Backend.device.pCmdQueues[ DX12_QUEUE_ID_DIRECT ];


	dx12Backend.memArenaBuffers = {
		.heapType = D3D12_HEAP_TYPE_DEFAULT,
		.usgFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS,
		.defaultBlockSize = 256 * MB
	};
	dx12Backend.memArenaTextures = {
		.heapType = D3D12_HEAP_TYPE_DEFAULT,
		.usgFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS,
		.defaultBlockSize = 256 * MB
	};
	dx12Backend.memArenaHostCom = {
		.heapType = D3D12_HEAP_TYPE_UPLOAD,
		.usgFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
		.defaultBlockSize = 256 * MB
	};

	// TODO: rethink 
	dx12Backend.descHeapResources = {
		.descType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.stride = pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ),
		.maxSize = UINT16_MAX,
		.count = 0
	};
	dx12Backend.descHeapSamplers = {
		.descType = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
		.stride = pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ),
		.maxSize = 8,
		.count = 0
	};

	D3D12_DESCRIPTOR_HEAP_DESC descHeapRscInfo = {
		.Type = dx12Backend.descHeapResources.descType,
		.NumDescriptors = dx12Backend.descHeapResources.maxSize,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	};
	HR_CHECK( pDevice->CreateDescriptorHeap( &descHeapRscInfo, IID_PPV_ARGS( &dx12Backend.descHeapResources.heap ) ) );

	D3D12_DESCRIPTOR_HEAP_DESC descHeapSamplersInfo = {
		.Type = dx12Backend.descHeapSamplers.descType,
		.NumDescriptors = dx12Backend.descHeapSamplers.maxSize,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	};
	HR_CHECK( pDevice->CreateDescriptorHeap( &descHeapSamplersInfo, IID_PPV_ARGS( &dx12Backend.descHeapSamplers.heap ) ) );


	// TODO: do it in the render loop as it can be dynamic
	DXGI_FORMAT scFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	dx12Backend.swapchain = Dx12MakeSwapchain( pDxgiFactory, pDirectQueue, hWnd, scFormat, SCREEN_WIDTH, SCREEN_HEIGHT, 3 );


	D3D12_ROOT_PARAMETER rootParameter = {};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameter.Constants.Num32BitValues = 10;
	rootParameter.Constants.RegisterSpace = 0;
	rootParameter.Constants.ShaderRegister = 0;
	D3D12_ROOT_SIGNATURE_DESC rootSignatureInfo = {};
	rootSignatureInfo.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
	rootSignatureInfo.NumParameters = 1;
	rootSignatureInfo.pParameters = &rootParameter;

	ID3DBlob* pSignatureBlob;
	ID3DBlob* pErrBlob;
	PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignatureProc =
		( PFN_D3D12_SERIALIZE_ROOT_SIGNATURE ) GetProcAddress( dx12AgilityDll, "D3D12SerializeRootSignature" );

	if( FAILED( D3D12SerializeRootSignatureProc(
		&rootSignatureInfo, D3D_ROOT_SIGNATURE_VERSION_1_0, &pSignatureBlob, &pErrBlob ) ) )
	{
		assert( pErrBlob->GetBufferSize() );
		std::cout << "DX12 ROOT SIGNATURE ERR: " << ( char* ) pErrBlob->GetBufferPointer() << '\n';
		abort();
	}

	HR_CHECK( pDevice->CreateRootSignature(
		0, pSignatureBlob->GetBufferPointer(), pSignatureBlob->GetBufferSize(), IID_PPV_ARGS( &dx12Backend.globalRootSignature ) ) );


	dxc_context dxc = DxcCreateContext();

	std::vector<u8> hlslBlob = SysReadFile( "Shaders/imgui.hlsl" );
	constexpr LPCWSTR vsDxcOptions[] = {
		L"-T", L"vs_6_6",
		L"-E", L"VsMain",
		L"-Zi" };
	
	IDxcBlob* vsDxilBlob = DxcCompileShader( hlslBlob, ( LPCWSTR* ) vsDxcOptions, std::size( vsDxcOptions ), false, dxc );

	constexpr LPCWSTR psDxcOptions[] = {
		L"-T", L"ps_6_6",
		L"-E", L"PsMain",
		L"-Zi" };

	IDxcBlob* psDxilBlob = DxcCompileShader( hlslBlob, ( LPCWSTR* ) psDxcOptions, std::size( psDxcOptions ), false, dxc );

	constexpr dx12_pso_config imguiPsoConfig = {
		.blend = TRUE,
		.srcBlend = D3D12_BLEND_SRC_ALPHA,
		.dstBlend = D3D12_BLEND_INV_SRC_ALPHA,
		.srcAlphaBlend = D3D12_BLEND_ONE,
		.dstAlphaBlend = D3D12_BLEND_INV_SRC_ALPHA,
		.fillMode = D3D12_FILL_MODE_SOLID,
		.cullMode = D3D12_CULL_MODE_NONE,
		.counterClockwiseFrontface = TRUE,
		.conservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
		.depthEnable = FALSE,
		.topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE
	};

	for( virtual_frame& vf : dx12Backend.virtualFrames ) vf = Dx12MakeVirtualFrame( pDevice );


}

#include "asset_compiler.h"
// TODO: proper file API
// TODO: will handle asset compilation elsewere
static void Dx12LoadDrakBinaryFromDisk( const char* assetPath )
{
	HANDLE hFile = CreateFileA( assetPath, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, 0 );
	if( hFile == INVALID_HANDLE_VALUE ) HR_CHECK( HRESULT_FROM_WIN32( GetLastError() ) );
	LARGE_INTEGER fileSize = {};
	if( !GetFileSizeEx( hFile, &fileSize ) ) HR_CHECK( HRESULT_FROM_WIN32( GetLastError() ) );


	auto pDevice = dx12Backend.device.pDevice;

	assert( fileSize.QuadPart < UINT32( -1 ) );
	auto[ hAlloc, pRsc, hostVisible ] = Dx12CreateUploadBuffer( UINT32( fileSize.QuadPart ), pDevice, dx12Backend.memArenaHostCom );

	
	OVERLAPPED asyncIo = {};
	if( !ReadFileEx( hFile, hostVisible, fileSize.QuadPart, &asyncIo, 0 ) )
		HR_CHECK( HRESULT_FROM_WIN32( GetLastError() ) );
	if( !CloseHandle( hFile ) ) HR_CHECK( HRESULT_FROM_WIN32( GetLastError() ) );

	const drak_file_footer& fileFooter = *( drak_file_footer* ) ( hostVisible + fileSize.QuadPart - sizeof( drak_file_footer ) );


	
}


void Dx12HostFrames( 
	UINT64 thisFrameIdx
){
	UINT64 thisFramebufferedIndex = thisFrameIdx % maxDx12FramesInFlight;

	const virtual_frame& thisVirtFrame = dx12Backend.virtualFrames[ thisFramebufferedIndex ];


}