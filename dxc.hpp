#include <vector>
#include <span>
#include <dxcapi.h>

#include "core_types.h"


// NOTE: massive help : https://simoncoenen.com/blog/programming/graphics/DxcCompiling + his github
struct dxc_context
{
	IDxcCompiler3* pCompiler;
	IDxcUtils* pUtils;
	IDxcValidator* pValidator;
	IDxcIncludeHandler* pIncludeHandler;

	// TODO: release memory
	// TODO: revisit
	// TODO: hot reloading
	// TODO: better spirv toggle ?
	IDxcBlob* CompileShader( const std::vector<u8>& hlslBlob, std::span<const wchar_t*> compileOptions, bool compileToSpirv );
};

dxc_context DxcCreateContext();
