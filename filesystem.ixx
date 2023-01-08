#include "core_types.h"
#include <string_view>
#include <vector>

export module filesystem;
// NOTE: inspired by https://github.com/simco50/FluxEngine/blob/master/FluxEngine/FileSystem/FileSystem.h

struct file_attributes
{
	size_t size = 0;
	bool isReadOnly = false;
	bool isDirectory = false;
};

struct FileVisitor
{
	FileVisitor() = default;
	virtual ~FileVisitor() = default;

	virtual bool Visit( const std::string& fileName, const bool isDirectory ) = 0;
	virtual bool IsRecursive() const = 0;
};

/*
class pak_filesystem
{
public:
	using MountPointPtr = UniquePtr<IMountPoint>;

	pak_filesystem();
	~pak_filesystem();

	static bool Mount( std::string_view physicalPath );

	static void AddPakLocation( std::string_view path );

	static std::unique_ptr<File> GetFile( std::string_view fileName );

	static bool Delete( std::string_view fileName );
	static bool Move( std::string_view fileName, std::string_view newFileName, const bool overWrite = true );
	static bool Copy( std::string_view fileName, std::string_view newFileName, const bool overWrite = true );

	static bool IterateDirectory( std::string_view path, FileVisitor& visitor );
	static void GetFilesInDirectory( std::string_view directory, std::vector<std::string_view>& files, const bool recursive );
	static void GetFilesWithExtension( std::string_view directory, std::vector<std::string_view>& files, std::string_view extension, const bool recursive );

private:
	static file_attributes GetFileAttributes( std::string_view filePath );

	static std::string FixPath( std::string_view path );
	static std::unique_ptr<IMountPoint> CreateMountPoint( std::string_view physicalPath );

	static std::vector<MountPointPtr> m_MountPoints;

	static std::vector<std::string_view> m_PakLocations;
};

export void MyFunc();
*/