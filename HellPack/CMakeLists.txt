set(THIRD_PARTY "${CMAKE_SOURCE_DIR}/3rdParty")

file(GLOB MESHOPT_SRCS "${THIRD_PARTY}/meshoptimizer/src/*.cpp")

# NOTE: HtLib's own .cpps are not listed here — they arrive as INTERFACE sources
#       through CommonDeps -> HtLib and are compiled into this exe.
add_executable(HellPack
    HellPack.cpp
    # 3rdParty sources — from CommonBuildSheet.props HellPack condition
    ${THIRD_PARTY}/bc7enc_rdo/bc7enc.cpp
    ${THIRD_PARTY}/bc7enc_rdo/rgbcx.cpp
    ${THIRD_PARTY}/miniz/miniz.c
    ${MESHOPT_SRCS}
)

target_include_directories(HellPack PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(HellPack PRIVATE _CONSOLE)

# Suppress static analysis on all 3rdParty sources  (mirrors CommonBuildSheet.props wildcard)
if(MSVC)
    set_source_files_properties(
        ${THIRD_PARTY}/bc7enc_rdo/bc7enc.cpp
        ${THIRD_PARTY}/bc7enc_rdo/rgbcx.cpp
        ${THIRD_PARTY}/miniz/miniz.c
        ${MESHOPT_SRCS}
        TARGET_DIRECTORY HellPack
        PROPERTIES COMPILE_OPTIONS "/analyze-"
    )
endif()
