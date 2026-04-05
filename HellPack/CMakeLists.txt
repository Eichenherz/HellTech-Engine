set(THIRD_PARTY "${CMAKE_SOURCE_DIR}/3rdParty")

add_executable(HellPack
    HellPack.cpp
    # 3rdParty sources — from CommonBuildSheet.props HellPack condition
    ${THIRD_PARTY}/bc7enc_rdo/bc7enc.cpp
    ${THIRD_PARTY}/bc7enc_rdo/rgbcx.cpp
    ${THIRD_PARTY}/MikkTSpace/mikktspace.c
    ${THIRD_PARTY}/miniz/miniz.c
    ${THIRD_PARTY}/meshoptimizer/src/allocator.cpp
    ${THIRD_PARTY}/meshoptimizer/src/clusterizer.cpp
    ${THIRD_PARTY}/meshoptimizer/src/indexanalyzer.cpp
    ${THIRD_PARTY}/meshoptimizer/src/indexcodec.cpp
    ${THIRD_PARTY}/meshoptimizer/src/indexgenerator.cpp
    ${THIRD_PARTY}/meshoptimizer/src/meshletcodec.cpp
    ${THIRD_PARTY}/meshoptimizer/src/meshletutils.cpp
    ${THIRD_PARTY}/meshoptimizer/src/overdrawoptimizer.cpp
    ${THIRD_PARTY}/meshoptimizer/src/partition.cpp
    ${THIRD_PARTY}/meshoptimizer/src/quantization.cpp
    ${THIRD_PARTY}/meshoptimizer/src/rasterizer.cpp
    ${THIRD_PARTY}/meshoptimizer/src/simplifier.cpp
    ${THIRD_PARTY}/meshoptimizer/src/spatialorder.cpp
    ${THIRD_PARTY}/meshoptimizer/src/stripifier.cpp
    ${THIRD_PARTY}/meshoptimizer/src/vcacheoptimizer.cpp
    ${THIRD_PARTY}/meshoptimizer/src/vertexcodec.cpp
    ${THIRD_PARTY}/meshoptimizer/src/vertexfilter.cpp
    ${THIRD_PARTY}/meshoptimizer/src/vfetchoptimizer.cpp
)

target_include_directories(HellPack PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(HellPack PRIVATE _CONSOLE)

# Suppress static analysis on all 3rdParty sources  (mirrors CommonBuildSheet.props wildcard)
if(MSVC)
    set_source_files_properties(
        ${THIRD_PARTY}/bc7enc_rdo/bc7enc.cpp
        ${THIRD_PARTY}/bc7enc_rdo/rgbcx.cpp
        ${THIRD_PARTY}/MikkTSpace/mikktspace.c
        ${THIRD_PARTY}/miniz/miniz.c
        ${THIRD_PARTY}/meshoptimizer/src/allocator.cpp
        ${THIRD_PARTY}/meshoptimizer/src/clusterizer.cpp
        ${THIRD_PARTY}/meshoptimizer/src/indexanalyzer.cpp
        ${THIRD_PARTY}/meshoptimizer/src/indexcodec.cpp
        ${THIRD_PARTY}/meshoptimizer/src/indexgenerator.cpp
        ${THIRD_PARTY}/meshoptimizer/src/meshletcodec.cpp
        ${THIRD_PARTY}/meshoptimizer/src/meshletutils.cpp
        ${THIRD_PARTY}/meshoptimizer/src/overdrawoptimizer.cpp
        ${THIRD_PARTY}/meshoptimizer/src/partition.cpp
        ${THIRD_PARTY}/meshoptimizer/src/quantization.cpp
        ${THIRD_PARTY}/meshoptimizer/src/rasterizer.cpp
        ${THIRD_PARTY}/meshoptimizer/src/simplifier.cpp
        ${THIRD_PARTY}/meshoptimizer/src/spatialorder.cpp
        ${THIRD_PARTY}/meshoptimizer/src/stripifier.cpp
        ${THIRD_PARTY}/meshoptimizer/src/vcacheoptimizer.cpp
        ${THIRD_PARTY}/meshoptimizer/src/vertexcodec.cpp
        ${THIRD_PARTY}/meshoptimizer/src/vertexfilter.cpp
        ${THIRD_PARTY}/meshoptimizer/src/vfetchoptimizer.cpp
        TARGET_DIRECTORY HellPack
        PROPERTIES COMPILE_OPTIONS "/analyze-"
    )
endif()
