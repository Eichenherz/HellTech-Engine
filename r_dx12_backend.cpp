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


// NOTE: Dx12 Agility SDK
extern "C" { __declspec( dllexport ) extern const UINT D3D12SDKVersion = 4; }
extern "C" { __declspec( dllexport ) extern LPCSTR D3D12SDKPath = ".\\D3D12\\"; }

struct dx12_device
{
	ID3D12Device9* pDevice = 0;
	ID3D12CommandQueue* pCmdQueue = 0;
	ID3D12Fence1* pFence = 0;
};
// TODO: 
inline static dx12_device Dx12CreateDeviceContext()
{
	typedef HRESULT( __stdcall* PFN_CreateDXGIFactory )( _In_ REFIID, _Out_ LPVOID* );

	HMODULE dx12AgilityDll = LoadLibraryA( "D3D12.dll" );
	assert( dx12AgilityDll );
	PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceProc = ( PFN_D3D12_CREATE_DEVICE )GetProcAddress( dx12AgilityDll, "D3D12CreateDevice" );
	PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterfaceProc =
		( PFN_D3D12_GET_DEBUG_INTERFACE )GetProcAddress( dx12AgilityDll, "D3D12GetDebugInterface" );
	PFN_CreateDXGIFactory CreateDXGIFactoryProc =
		( PFN_CreateDXGIFactory ) GetProcAddress( LoadLibraryA( "dxgi.dll" ), "CreateDXGIFactory" );
	
#ifdef DX12_DEBUG
	ID3D12Debug5* pDebug = 0;
	HR_CHECK( D3D12GetDebugInterfaceProc( IID_PPV_ARGS( &pDebug ) ) );
	pDebug->EnableDebugLayer();
	pDebug->Release();
#endif

	IDXGIFactory7* pDxgiFactory = 0;
	HR_CHECK( CreateDXGIFactoryProc( IID_PPV_ARGS( &pDxgiFactory ) ) );

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
		D3D12_FEATURE_DATA_D3D12_OPTIONS d12Options = { .ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3 };
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
	pInfoQueue->Release();
#endif // DX12_DEBUG

	return{ pDevice,pCmdQueue,pFence };
}

static dx12_device dx12Device;

inline void Dx12BackendInit()
{
	dx12Device = Dx12CreateDeviceContext();
}