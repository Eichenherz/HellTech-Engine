find_package(Vulkan REQUIRED)   # reads VULKAN_SDK env var; provides Vulkan::Vulkan

set(THIRD_PARTY "${CMAKE_SOURCE_DIR}/3rdParty")

# Glob Engine's own sources — r_dx12_backend.cpp is excluded (<None> in vcxproj)
file(GLOB_RECURSE ENGINE_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)
list(REMOVE_ITEM ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/r_dx12_backend.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/dxc.cpp" )

add_executable(Engine WIN32      # WIN32 = Windows subsystem (not console)
    ${ENGINE_SRC}
    "${CMAKE_SOURCE_DIR}/HtLib/Win32/win32_impl.cpp"   # virtual_arena Win32 impl
    # 3rdParty sources — from CommonBuildSheet.props Engine condition
    ${THIRD_PARTY}/imgui/imgui.cpp
    ${THIRD_PARTY}/imgui/imgui_draw.cpp
    ${THIRD_PARTY}/imgui/imgui_tables.cpp
    ${THIRD_PARTY}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY}/ImGuiFileDialog/ImGuiFileDialog.cpp
    ${THIRD_PARTY}/miniz/miniz.c
    ${THIRD_PARTY}/OffsetAllocator/offsetAllocator.cpp
)

target_include_directories(Engine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"          # $(ProjectDir) — Engine/ itself
    "${CMAKE_SOURCE_DIR}/HellPack"         # shared types from HellPack dir (Engine.vcxproj IncludePath)
    "$ENV{VULKAN_SDK}/Source"              # Vulkan SDK source headers
    "$ENV{VULKAN_SDK}/Include/vulkan"      # allows #include <vulkan.h> directly (mirrors vcxproj IncludePath)
    "$ENV{VULKAN_SDK}/Include/dxc"         # dxcapi.h  (used by dxc.h via #include <dxcapi.h>)
)

target_link_libraries(Engine PRIVATE
    Vulkan::Vulkan
    "$ENV{VULKAN_SDK}/Lib/dxcompiler.lib"
)

target_compile_definitions(Engine PRIVATE _CONSOLE)

# No incremental linking for Debug  (LinkIncremental=false in Engine.vcxproj Debug)
if(MSVC)
    target_link_options(Engine PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL:NO>)
endif()

# Suppress static analysis on 3rdParty sources + spirv_reflect.c
if(MSVC)
    set_source_files_properties(
        spirv_reflect.c
        ${THIRD_PARTY}/imgui/imgui.cpp
        ${THIRD_PARTY}/imgui/imgui_draw.cpp
        ${THIRD_PARTY}/imgui/imgui_tables.cpp
        ${THIRD_PARTY}/imgui/imgui_widgets.cpp
        ${THIRD_PARTY}/ImGuiFileDialog/ImGuiFileDialog.cpp
        ${THIRD_PARTY}/miniz/miniz.c
        TARGET_DIRECTORY Engine
        PROPERTIES COMPILE_OPTIONS "/analyze-"
    )
endif()
