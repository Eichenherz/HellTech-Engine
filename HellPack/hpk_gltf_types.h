#pragma once

#ifndef HELLTECH_HPK_GLTF_TYPES_H
#define HELLTECH_HPK_GLTF_TYPES_H

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_fixed_string.h>
#include <ht_mem_arena.h>

#include <glaze/json.hpp>

#include <array>
#include <span>
#include <string_view>

enum class caldera_prim_purpose_t : u8 { NONE = 0, COLLISION, SHADOW, SKY, DECAL, GUIDE, INVISIBLE, INTERPOLATED_PATCH, BRUSH, DEGENERATE, UNKNOWN };

enum class hpk_gltf_geom_type : u8 { NONE = 0, TERRAIN };

enum class hpk_gltf_accessor_type : u8 { SCALAR = 0, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4 };

enum class hpk_gltf_prim_mode : u32 { POINTS = 0, LINES, LINE_LOOP, LINE_STRIP, TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN };

enum class hpk_gltf_component_type : u32
{
    NONE            = 0,
    BYTE            = 5120,
    UNSIGNED_BYTE   = 5121,
    SHORT           = 5122,
    UNSIGNED_SHORT  = 5123,
    UNSIGNED_INT    = 5125,
    FLOAT           = 5126
};

struct hpk_gltf_caldera_player_samples
{
    u32 time    = ~0u;
    u32 life    = ~0u;
    u32 opacity = ~0u;
    u32 color   = ~0u;
};

struct hpk_gltf_caldera_mesh_purpose
{
    caldera_prim_purpose_t purpose = caldera_prim_purpose_t::NONE;
};

struct hpk_gltf_caldera_shared_mesh
{
    std::string_view    parentFile  = {};
    std::string_view    meshName    = {};
    u32                 meshIdx     = 0; // NOTE: theoretically incorrect, but makes no sense to have NO parent fileName
};

struct hpk_gltf_caldera_geom_type
{
    hpk_gltf_geom_type type = hpk_gltf_geom_type::NONE;
};

struct hpk_gltf_khr_node_visibility
{
    bool visible = true;
};

struct hpk_gltf_caldera_node_place_holder
{
    bool placeholder = false;
};

struct hpk_gltf_ext_mesh_gpu_instancing_attributes
{
    u32 TRANSLATION = ~0u;
    u32 ROTATION    = ~0u;
    u32 SCALE       = ~0u;
};

struct hpk_gltf_ext_mesh_gpu_instancing
{
    hpk_gltf_ext_mesh_gpu_instancing_attributes attributes = {};

    bool Enabled() const
    {
        return ( ~0u == attributes.TRANSLATION )
            && ( ~0u == attributes.ROTATION )
            && ( ~0u == attributes.SCALE );
    }
};

struct hpk_gltf_node_extensions
{
    hpk_gltf_caldera_player_samples     CALDERA_player_samples      = {};
    hpk_gltf_caldera_node_place_holder  CALDERA_node_place_holder   = {};
    hpk_gltf_khr_node_visibility        KHR_node_visibility         = {};
    hpk_gltf_ext_mesh_gpu_instancing    EXT_mesh_gpu_instancing     = {};
};

struct hpk_gltf_skin
{
    std::string_view        name                = {};
    u32                     inverseBindMatrices = ~0u;
    u32                     skeleton            = ~0u;
    std::span<const u32>    joints              = {};
};

struct hpk_gltf_node
{
    std::string_view            name        = {};
    u32                         mesh        = ~0u;
    u32                         skin        = ~0u;
    std::span<const u32>        children    = {};
    std::array<float, 16>       matrix      = {};
    std::array<float, 3>        translation = { 0.0f, 0.0f, 0.0f };
    std::array<float, 4>        rotation    = { 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3>        scale       = { 1.0f, 1.0f, 1.0f };
    hpk_gltf_node_extensions    extensions  = {};
};

// NOTE: indices of ACCESSOR
struct hpk_gltf_prim_attributes
{
    u32 POSITION    = ~0u;
    u32 NORMAL      = ~0u;
    u32 TANGENT     = ~0u;
    u32 TEXCOORD_0  = ~0u;
    u32 TEXCOORD_1  = ~0u;
};

struct hpk_gltf_prim_extensions
{
    hpk_gltf_caldera_geom_type          CALDERA_geom_type           = {};
};

struct hpk_gltf_primitive
{
    hpk_gltf_prim_attributes    attributes  = {};
    u32                         indices     = ~0u; // NOTE: idx of INDICES accessor
    u32                         material    = ~0u;
    hpk_gltf_prim_mode          mode        = hpk_gltf_prim_mode::TRIANGLES;
    hpk_gltf_prim_extensions    extensions  = {};
};

struct hpk_gltf_caldera_lods
{
    std::string_view                        name    = {};
    std::span<const std::array<u32, 2>>     lods    = {};
};

struct hpk_gltf_mesh_extensions
{
    hpk_gltf_caldera_shared_mesh    CALDERA_shared_mesh     = {};
    hpk_gltf_caldera_mesh_purpose   CALDERA_mesh_purpose    = {};
    hpk_gltf_caldera_lods           CALDERA_lods            = {};
};

struct hpk_gltf_mesh
{
    std::string_view                    name        = {};
    std::span<const hpk_gltf_primitive> primitives  = {};
    hpk_gltf_mesh_extensions            extensions  = {};
};

struct hpk_gltf_accessor
{
    u32                     bufferView      = ~0u;
    u64                     byteOffset      = 0;
    hpk_gltf_component_type componentType   = hpk_gltf_component_type::NONE;
    bool                    normalized      = false;
    u32                     count           = 0;
    hpk_gltf_accessor_type  type            = hpk_gltf_accessor_type::SCALAR;
    std::span<const float>  min             = {};
    std::span<const float>  max             = {};
};

struct hpk_gtlf_buffer_view
{
    u32 buffer      = 0;
    u64 byteOffset  = 0;
    u64 byteLength  = 0;
    u32 byteStride  = 0;
    u32 target      = 0;
};

struct hpk_gltf_scene
{
    std::string_view        name    = {};
    std::span<const u32>    nodes   = {};
};

struct hpk_gltf_data
{
    u32                                     scene       = ~0u;
    std::span<const hpk_gltf_scene>         scenes      = {};
    std::span<const hpk_gltf_node>          nodes       = {};
    std::span<const hpk_gltf_mesh>          meshes      = {};
    std::span<const hpk_gltf_skin>          skins       = {};
    std::span<const hpk_gltf_accessor>      accessors   = {};
    std::span<const hpk_gtlf_buffer_view>   bufferViews = {};
    std::span<const u8>                     binChunk    = {};
};

template<arena_t ARENA_T>
struct hpk_glz_ctx
{
    // NOTE: the case on fields like custom_error_message matters !
    std::string_view    custom_error_message    = {};
    ARENA_T*            pArena                  = nullptr;
    glz::error_code     error                   = {};
    u32                 depth                   = 0;
    bool                padded_input            = false;

};

constexpr glz::opts HPK_GLB_GLZ_OPTS = { .null_terminated = false, .error_on_unknown_keys = false };

using namespace std::string_view_literals;

template<>
struct glz::meta<caldera_prim_purpose_t>
{
    using enum caldera_prim_purpose_t;
    static constexpr std::array keys = { "collision"sv, "shadow"sv, "sky"sv, "decal"sv, "guide"sv, "invisible"sv, "interpolated_patch"sv, "brush"sv, "degenerate"sv };
    static constexpr std::array value = { COLLISION, SHADOW, SKY, DECAL, GUIDE, INVISIBLE, INTERPOLATED_PATCH, BRUSH, DEGENERATE };
};

template<>
struct glz::meta<hpk_gltf_geom_type>
{
    static constexpr std::array keys = { "terrain"sv };
    static constexpr std::array value = { hpk_gltf_geom_type::TERRAIN };
};

template<>
struct glz::meta<hpk_gltf_accessor_type>
{
    using enum hpk_gltf_accessor_type;

    static constexpr std::array keys = { "SCALAR"sv, "VEC2"sv, "VEC3"sv, "VEC4"sv, "MAT2"sv, "MAT3"sv, "MAT4"sv };
    static constexpr std::array value = { SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4 };
};

template<typename T>
struct glz::meta<std::span<const T>>
{
    static constexpr bool custom_read = true;
};

template<typename T>
struct glz::from<glz::JSON, std::span<const T>>
{
    template<auto OPTS>
    static void op( std::span<const T>& value, glz::is_context auto&& ctx, auto&& it, auto end )
    {
        constexpr auto Opts = glz::ws_handled_off<OPTS>();

        if( glz::skip_ws<Opts>( ctx, it, end ) ) return;
        if( glz::match_invalid_end<'[', Opts>( ctx, it, end ) ) return;
        if( glz::enter_depth( ctx ) ) return;
        if( glz::skip_ws<Opts>( ctx, it, end ) ) return;

        u64 count = glz::number_of_array_elements<Opts>( ctx, it, end );
        if( bool( ctx.error ) ) return;

        std::span elems = ArenaNewArray<T>( *ctx.pArena, count );
        for( u64 i = 0; i < count; ++i )
        {
            if( i && glz::match_invalid_end<',', Opts>( ctx, it, end ) ) return;
            glz::parse<glz::JSON>::op<Opts>( elems[ i ], ctx, it, end );
            if( bool( ctx.error ) ) return;
            if( glz::skip_ws<Opts>( ctx, it, end ) ) return;
        }
        glz::match<']'>( ctx, it );
        --ctx.depth;

        value = elems;
    }
};

template<u64 N>
struct glz::meta<fixed_string<N>>
{
    static constexpr bool custom_read = true;
};

template<u64 N>
struct glz::from<glz::JSON, fixed_string<N>>
{
    template<auto OPTS>
    static void op( fixed_string<N>& value, glz::is_context auto&& ctx, auto&& it, auto end )
    {
        std::string_view str = {};
        glz::parse_transient_string_view<OPTS>( str, ctx, it, end );
        if( glz::parse_failed( ctx.error ) ) return;
        if( std::size( str ) > N )
        {
            ctx.error = glz::error_code::invalid_length;
            return;
        }
        value = str;
    }
};

constexpr u32 HPK_GLB_MAGIC             = 0x46546C67;
constexpr u32 HPK_GLB_CHUNK_TYPE_JSON   = 0x4E4F534A;
constexpr u32 HPK_GLB_CHUNK_TYPE_BIN    = 0x004E4942;

struct hpk_glb_header
{
    u32 magic;
    u32 version;
    u32 length;
};

struct hpk_glb_chunk_header
{
    u32 length;
    u32 type;
};

hpk_gltf_data HpkGltfParseFromBytes( std::span<const u8> bytes, arena_t auto& arena )
{
    const u8* pCursor = &bytes[ 0 ];

    hpk_glb_header glbHdr = *( const hpk_glb_header* ) pCursor;
    HT_ASSERT( HPK_GLB_MAGIC == glbHdr.magic );
    pCursor += sizeof( glbHdr );

    hpk_glb_chunk_header jsonHdr = *( const hpk_glb_chunk_header* ) pCursor;
    HT_ASSERT( HPK_GLB_CHUNK_TYPE_JSON == jsonHdr.type );
    pCursor += sizeof( jsonHdr );

    auto jsonView = std::string_view{ ( const char* ) pCursor, jsonHdr.length };

    pCursor += jsonHdr.length;
    hpk_glb_chunk_header binHdr = *( const hpk_glb_chunk_header* ) pCursor;
    HT_ASSERT( HPK_GLB_CHUNK_TYPE_BIN == binHdr.type );
    pCursor += sizeof( binHdr );

    auto ctx = hpk_glz_ctx{ .pArena = &arena };
    hpk_gltf_data data = {};
    glz::error_ctx err = glz::read<HPK_GLB_GLZ_OPTS>( data, jsonView, ctx );
    HT_ASSERT( !err );

    data.binChunk = std::span{ pCursor, binHdr.length };
    return data;
}

#endif //!HELLTECH_HPK_GLTF_TYPES_H