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
#include <dxcapi.h>

#define HR_CHECK( func )												\
do{																		\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR"\nERR: ";	\
	HRESULT hr = func;													\
	if( !SUCCEEDED( hr ) ){                                             \
        _com_error err = { hr };                                        \
		char dbgStr[256] = {};											\
		strcat_s( dbgStr, sizeof( dbgStr ), DEV_ERR_STR );				\
		strcat_s( dbgStr, sizeof( dbgStr ), err.ErrorMessage() );	    \
		SysErrMsgBox( dbgStr );											\
		constexpr UINT behaviour = MB_OK | MB_ICONERROR | MB_APPLMODAL; \
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
extern "C" { __declspec( dllexport ) extern const UINT D3D12SDKVersion = 4; }
extern "C" { __declspec( dllexport ) extern LPCSTR D3D12SDKPath = ".\\D3D12\\"; }

struct dx12_device
{
	ID3D12Device9* pDevice = 0;
	ID3D12CommandQueue* pCmdQueue = 0;
	ID3D12Fence1* pFence = 0;
};


inline static dx12_device Dx12CreateDeviceContext( IDXGIFactory7* pDxgiFactory )
{
	HMODULE dx12AgilityDll = LoadLibraryA( "D3D12.dll" );
	assert( dx12AgilityDll );
	PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceProc = ( PFN_D3D12_CREATE_DEVICE )GetProcAddress( dx12AgilityDll, "D3D12CreateDevice" );
	PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterfaceProc =
		( PFN_D3D12_GET_DEBUG_INTERFACE )GetProcAddress( dx12AgilityDll, "D3D12GetDebugInterface" );
	
#ifdef DX12_DEBUG
	ID3D12Debug5* pDebug = 0;
	HR_CHECK( D3D12GetDebugInterfaceProc( IID_PPV_ARGS( &pDebug ) ) );
	pDebug->EnableDebugLayer();
	pDebug->Release();
#endif

	ID3D12Device9* pDevice = 0;
	IDXGIAdapter4* pGpu = 0;
	DXGI_ADAPTER_DESC3 pGpuInfo = {};
	for( UINT i = 0; 
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


// TODO: add memory checks ?
struct dx12_allocation
{
	ID3D12Heap* mem;
	UINT64 size;
	UINT64 allocated;
};
// TODO: handle heap, uasge, rsc desc better
// TODO: how to handle CUSTOM HEAPS when time comes ?
struct dx12_mem_arena
{
	std::vector<dx12_allocation> allocs;
	D3D12_HEAP_TYPE heapType;
	D3D12_HEAP_FLAGS usgFlags;
	UINT defaultBlockSize;
};



// NOTE: RTs will be alloc-ed separately
// TODO: host visible textures ?
static dx12_mem_arena dx12BufferArena;
static dx12_mem_arena dx12TextureArena;
static dx12_mem_arena dx12HostComArena;

// TODO: check sizes and stuff
// TODO: allow other uses ?
// TODO: no asserts
inline static D3D12_RESOURCE_DESC Dx12WriteBufferDesc( UINT size, D3D12_RESOURCE_FLAGS usg = D3D12_RESOURCE_FLAG_NONE )
{
	assert( usg == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS || usg == D3D12_RESOURCE_FLAG_NONE );

	D3D12_RESOURCE_DESC rscDesc = {};
	rscDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rscDesc.Alignment = 0;
	rscDesc.Width = size;
	rscDesc.Height = rscDesc.DepthOrArraySize = rscDesc.MipLevels = 1;
	rscDesc.Format = DXGI_FORMAT_UNKNOWN;
	rscDesc.SampleDesc = { 1,0 };
	rscDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rscDesc.Flags = ( usg == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) ? usg : D3D12_RESOURCE_FLAG_NONE;

	return rscDesc;
}

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

// TODO: handle GPU write back
inline D3D12_HEAP_PROPERTIES Dx12MakeHeapPropsFromType( 
	D3D12_HEAP_TYPE heapTypeFlags
){
	assert( heapTypeFlags != D3D12_HEAP_TYPE_CUSTOM );
	D3D12_HEAP_PROPERTIES props = {};
	props.Type = heapTypeFlags;
	props.CPUPageProperty = ( heapTypeFlags == D3D12_HEAP_TYPE_UPLOAD ) ? 
		D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE: D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	return props;
}

inline static ID3D12Resource* Dx12CreateCommittedResource(
	ID3D12Device9* pDevice,
	const D3D12_RESOURCE_DESC& rscDesc,
	D3D12_HEAP_TYPE heapType
){
	D3D12_HEAP_PROPERTIES heapProps = Dx12MakeHeapPropsFromType( heapType );
	D3D12_RESOURCE_STATES rscInitialState = ( heapType == D3D12_HEAP_TYPE_UPLOAD ) ?
		D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	ID3D12Resource* pRsc = 0;
	HR_CHECK( pDevice->CreateCommittedResource( 
		&heapProps, D3D12_HEAP_FLAG_NONE, &rscDesc, D3D12_RESOURCE_STATE_COMMON, 0, IID_PPV_ARGS( &pRsc ) ) );

	return pRsc;
}
// TODO: no assert
// TODO: heap alloc into it's own func ?
// TODO: clear val ?
inline static ID3D12Resource* Dx12CreatePlacedResource(
	ID3D12Device9* pDevice,
	const D3D12_RESOURCE_DESC& rscDesc, 
	dx12_mem_arena& rscArena 
){
	D3D12_RESOURCE_ALLOCATION_INFO allocInfo = pDevice->GetResourceAllocationInfo( 0, 1, &rscDesc );
	assert( allocInfo.SizeInBytes != UINT64_MAX );

	dx12_allocation lastAlloc = ( !std::size( rscArena.allocs ) ) ? 
		dx12_allocation{} : rscArena.allocs[ std::size( rscArena.allocs ) - 1 ];
	UINT64 alignedAllocated = FwdAlign( lastAlloc.allocated, allocInfo.Alignment );
	if( alignedAllocated + allocInfo.SizeInBytes > lastAlloc.size )
	{
		D3D12_HEAP_DESC heapDesc = {};
		heapDesc.SizeInBytes = allocInfo.SizeInBytes;
		heapDesc.Properties = Dx12MakeHeapPropsFromType( rscArena.heapType );
		heapDesc.Alignment = allocInfo.Alignment;
		heapDesc.Flags = rscArena.usgFlags;

		ID3D12Heap* pHeap = 0;
		HR_CHECK( pDevice->CreateHeap( &heapDesc, IID_PPV_ARGS( &pHeap ) ) );

		rscArena.allocs.push_back( { pHeap, heapDesc.SizeInBytes, 0 } );
	}

	ID3D12Heap* pMem = rscArena.allocs[ std::size( rscArena.allocs ) - 1 ].mem;
	UINT64 offset = rscArena.allocs[ std::size( rscArena.allocs ) - 1 ].allocated ;
	rscArena.allocs[ std::size( rscArena.allocs ) - 1 ].allocated = alignedAllocated + allocInfo.SizeInBytes;


	D3D12_RESOURCE_STATES rscInitialState = ( rscArena.heapType == D3D12_HEAP_TYPE_UPLOAD ) ? 
		D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	ID3D12Resource* pRsc = 0;
	HR_CHECK( pDevice->CreatePlacedResource( pMem, offset, &rscDesc, D3D12_RESOURCE_STATE_COMMON, 0, IID_PPV_ARGS( &pRsc ) ) );
	
	return pRsc;
}

inline void Dx12BackendInit()
{
	typedef HRESULT( __stdcall* PFN_CreateDXGIFactory )( _In_ REFIID, _Out_ LPVOID* );
	PFN_CreateDXGIFactory CreateDXGIFactoryProc =
		( PFN_CreateDXGIFactory ) GetProcAddress( LoadLibraryA( "dxgi.dll" ), "CreateDXGIFactory" );

	IDXGIFactory7* pDxgiFactory = 0;
	HR_CHECK( CreateDXGIFactoryProc( IID_PPV_ARGS( &pDxgiFactory ) ) );

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

	ID3D12Device9* pDevice = dx12Device.pDevice;
	constexpr UINT maxDescHeapSize = UINT16_MAX;
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = maxDescHeapSize;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ID3D12DescriptorHeap* pDescMem = 0;
	HR_CHECK( pDevice->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &pDescMem ) ) );
}