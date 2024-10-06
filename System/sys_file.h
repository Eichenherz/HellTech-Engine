#ifndef __HT_FILE__
#define __HT_FILE__

#include "core_types.h"
#include <span>
#include <string_view>
#include <memory>

// TODO: add more stuff as needed
enum class file_permissions
{
	READ,
	WRITE
};

struct file
{
	virtual size_t size() = 0;
	virtual u8* data() = 0;
	virtual u64 timestamp() = 0;
	virtual ~file() = 0;
};

// TODO: add more stuff as needed
std::unique_ptr<file> SysCreateFile( std::string_view path, file_permissions filePermissions );

#endif // !__HT_FILE__
