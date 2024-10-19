#include <System/Win32/DEFS_WIN32_NO_BS.h>
#include <Windows.h>
#include <comdef.h>

#include "dxc.hpp"

#include <assert.h>

#include <System/sys_platform.hpp>
#include <System/sys_file.hpp>
#include <System/sys_filesystem.hpp>
#include <iostream>
#include <System/Win32/win32_utils.hpp>

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

// TODO: check stuff
// TODO: no assert
inline dxc_context DxcCreateContext()
{
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

struct DxcIncludeHandler : public IDxcIncludeHandler
{
	struct cached_blob
	{
		std::wstring_view key;
		IDxcBlobEncoding* pEncoding;
	};
	std::vector<cached_blob> includeCache;
	dxc_context& ctx;

	DxcIncludeHandler( dxc_context& _ctx ) : ctx{ _ctx } {}

	HRESULT STDMETHODCALLTYPE LoadSource( _In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource ) override
	{
		constexpr LPCWSTR pValidExtensions[] = { L"hlsli", L"h" };

		path includePath = { pFilename };
		if( ( includePath.extension() != pValidExtensions[ 0 ] ) || ( includePath.extension() != pValidExtensions[ 1 ] ) )
		{
			std::cout << "Include path does not have a valid extension." << '\n';
			return E_FAIL;
		}

		for( auto includeView : this->includeCache )
		{
			if( includeView.key == includePath )
			{
				*ppIncludeSource = includeView.pEncoding;
				return S_OK;
			}
		}

		auto pFile = SysCreateFile( includePath.string(), file_permissions::READ );

		IDxcBlobEncoding* pEncoding;
		HRESULT hr = ctx.pUtils->CreateBlob( pFile->data(), ( u32 ) pFile->size(), 0, &pEncoding );
		if( SUCCEEDED( hr ) )
		{

			this->includeCache.push_back( { includePath.c_str(), pEncoding } );
			*ppIncludeSource = pEncoding;
		}
		return hr;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject ) override
	{
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return 0; }
	ULONG STDMETHODCALLTYPE Release() override { return 0; }
};

IDxcBlob* dxc_context::CompileShader( const std::vector<u8>& hlslBlob, std::span<const wchar_t*> compileOptions, bool compileToSpirv ) 
{
	static_assert( std::same_as<const wchar_t*, LPCWSTR> );

	DxcBuffer hlslFileDesc = { std::data( hlslBlob ), std::size( hlslBlob ) };

	DxcIncludeHandler includeHandler = { *this };

	IDxcResult* pCompileResult;
	HR_CHECK( pCompiler->Compile(
		&hlslFileDesc,
		std::data( compileOptions ),
		( UINT32 ) std::size( compileOptions ),
		&includeHandler,
		IID_PPV_ARGS( &pCompileResult ) )
	);

	IDxcBlobUtf8* pErrors;
	HR_CHECK( pCompileResult->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( &pErrors ), 0 ) );
	if( pErrors && pErrors->GetStringLength() > 0 )
	{
		std::cout << "DXC Compile Error: " << ( char* ) pErrors->GetBufferPointer() << '\n';
		assert( 0 );
		return {};
	}

	IDxcBlob* pShaderBlob;
	HR_CHECK( pCompileResult->GetOutput( DXC_OUT_OBJECT, IID_PPV_ARGS( &pShaderBlob ), 0 ) );

	if( !compileToSpirv )
	{
		IDxcOperationResult* pResult;
		HR_CHECK( pValidator->Validate( ( IDxcBlob* ) pShaderBlob, DxcValidatorFlags_InPlaceEdit, &pResult ) );
		HRESULT validationResult;
		HR_CHECK( pResult->GetStatus( &validationResult ) );
		if( validationResult != S_OK )
		{
			IDxcBlobEncoding* pPrintBlob;
			IDxcBlobUtf8* pPrintBlobUtf8;
			HR_CHECK( pResult->GetErrorBuffer( &pPrintBlob ) );
			HR_CHECK( pUtils->GetBlobAsUtf8( pPrintBlob, &pPrintBlobUtf8 ) );

			std::cout << "DXC Validation Error: " << ( char* ) pPrintBlobUtf8->GetBufferPointer() << '\n';
			assert( 0 );
			return{};
		}
	}

	return pShaderBlob;
}
