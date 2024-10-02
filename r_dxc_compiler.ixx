#include <Windows.h>
#include <vector>
#include <span>
#include <dxcapi.h>
#include <assert.h>
#include <comdef.h>
#include <string>
#include <iostream>


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


export module r_dxc_compiler;
// TODO: write own ShaderBlob to control memory ?
// NOTE: massive help : https://simoncoenen.com/blog/programming/graphics/DxcCompiling + his github
export{
	struct shader_file_keyval
	{
		UINT64 key;
		UINT64 timestamp;
		IDxcBlobEncoding* pEncoding;
	};
	struct dxc_context
	{
		IDxcCompiler3* pCompiler;
		IDxcUtils* pUtils;
		IDxcValidator* pValidator;
		IDxcIncludeHandler* pIncludeHandler;

		std::vector<shader_file_keyval> includeHashmap;

		IDxcBlob* CompileShader( 
			const std::vector<u8>& hlslBlob, LPCWSTR* compileOptions, UINT64 compileOptionsCount, bool compileToSpirv );
	};

	dxc_context DxcCreateContext();
	
}


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

// TODO: revisit
// TODO: hot reloading
inline auto DxcLoadHeaderBlob(
	LPCWSTR shaderFilename,
	IDxcUtils* pUtils,
	std::vector<shader_file_keyval>& fileHashmap
) {
	struct __retval
	{
		IDxcBlobEncoding* pEncoding;
		HRESULT hr;
	};

	constexpr LPCWSTR pValidExtensions[] = { L"hlsli", L"h" };

	std::wstring_view pathView = { shaderFilename };

	std::wstring_view extView = { shaderFilename + pathView.find_last_of( L"." ) + 1 };
	if( extView != pValidExtensions[ 0 ] && extView != pValidExtensions[ 1 ] )
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
	std::span<LPCWSTR> compileOptions,
	bool compileToSpirv,
	dxc_context& ctx
) {
	// NOTE: stupid OOPs interface
	struct CustomIncludeHandler : public IDxcIncludeHandler
	{
		dxc_context& ctx;

		inline CustomIncludeHandler( dxc_context& _ctx ) : ctx{ _ctx } {}

		HRESULT STDMETHODCALLTYPE
			LoadSource( _In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource ) override
		{
			auto [pEncodingBlob, hr] = DxcLoadHeaderBlob( pFilename, ctx.pUtils, ctx.includeHashmap );
			*ppIncludeSource = pEncodingBlob;
			return hr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject ) override
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
		&hlslFileDesc, std::data( compileOptions ), std::szie( compileOptions ), &includeHandler, IID_PPV_ARGS( &pCompileResult ) ) );

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