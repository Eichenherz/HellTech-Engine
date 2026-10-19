const std = @import( "std" );
const zt = @import( "ZpkTypes.zig" );

// NOTE: types
pub const gltf_node = struct
{
    name:           ?[]const u8 = null,
    mesh:           ?u32        = null,
    children:       []const u32 = &.{},
    matrix:         ?[ 16 ]f32  = null,
    translation:    ?[ 3 ]f32   = null,
    rotation:       ?[ 4 ]f32   = null,
    scale:          ?[ 3 ]f32   = null,
    extensions:     struct
    {
        CALDERA_player_samples:     ?gltf_caldera_player_samples    = null,
        KHR_node_visibility:        ?gltf_khr_node_visibility       = null,
        EXT_mesh_gpu_instancing:    ?gltf_ext_mesh_gpu_instancing   = null,
        MSFT_lod:                   ?gltf_msft_lod                  = null,
    } = .{},

    pub fn GetTrs( self: *const gltf_node ) zt.trs_t
    {
        if( self.matrix ) | m | return zt.Mat4x4ToTrs( m );

        return .{
            .t = self.translation   orelse ( zt.trs_t{} ).t,
            .r = self.rotation      orelse ( zt.trs_t{} ).r,
            .s = self.scale         orelse ( zt.trs_t{} ).s,
        };
    }

    pub fn GetInstCount( self: *const gltf_node, accessors: []const gltf_accessor ) u64
    {
        const inst = self.extensions.EXT_mesh_gpu_instancing orelse return 1;
        const attrs = inst.attributes;
        return accessors[ attrs.TRANSLATION orelse attrs.ROTATION orelse attrs.SCALE.? ].count;
    }
};

pub const gltf_primitive = struct
{
    attributes: struct
    {
        POSITION:   ?u32 = null,
        NORMAL:     ?u32 = null,
        TANGENT:    ?u32 = null,
        TEXCOORD_0: ?u32 = null,
        TEXCOORD_1: ?u32 = null,
    },
    indices:    ?u32    = null,
    material:   ?u32    = null,
    mode:       u32     = 4,
    extensions: struct
    {
        CALDERA_primitive_purpose:  ?gltf_caldera_primitive_purpose = null,
        CALDERA_shared_mesh:        ?gltf_caldera_shared_mesh       = null,
        CALDERA_geom_type:          ?gltf_caldera_geom_type         = null,
    } = .{},
};

pub const gltf_mesh = struct
{
    name:       ?[]const u8 = null,
    primitives: []const gltf_primitive,
};

pub const gltf_accessor_type = enum { SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4 };

pub const gltf_accessor = struct
{
    bufferView:     ?u32            = null,
    byteOffset:     u64             = 0,
    componentType:  u32,
    normalized:     bool            = false,
    count:          u32,
    type:           gltf_accessor_type,
    min:            ?[]const f32    = null,
    max:            ?[]const f32    = null,
};

pub const gtlf_buffer_view = struct
{
    buffer:     u32,
    byteOffset: u64 = 0,
    byteLength: u64,
    byteStride: ?u32 = null,
    target:     ?u32 = null,
};

pub const gltf_scene = struct
{
    name:   ?[]const u8 = null,
    nodes:  []const u32 = &.{},
};

pub const gltf_data = struct
{
    scene:          ?u32                        = null,
    scenes:         []const gltf_scene          = &.{},
    nodes:          []const gltf_node           = &.{},
    meshes:         []const gltf_mesh           = &.{},
    accessors:      []const gltf_accessor       = &.{},
    bufferViews:    []const gtlf_buffer_view    = &.{},
};

pub fn GlbTraverseHierachyRecursive( glbData: *const gltf_data ) void
{
    if( null == glbData.scene ) @panic( "No base scene in glb" );

    const LmbdVisitNode = struct
    {
        fn Pfn( data: *const gltf_data, nodeIdx: u32, parentTrs: zt.trs_t ) void
        {
            const currNode: *const gltf_node = &data.nodes[ nodeIdx ];

            if( currNode.extensions.KHR_node_visibility ) | vis | if( !vis.visible ) return;

            const trs: zt.trs_t = currNode.GetTrs().TrsCompose( parentTrs );

            if( currNode.mesh ) | meshIdx |
            {
                const instCount: u64 = gltf_node.GetInstCount( currNode, data.accessors );

                const mesh: *const gltf_mesh = &data.meshes[ meshIdx ];

                for( mesh.primitives, 0.. ) | prim, primIdx |
                {
                    if( null != prim.extensions.CALDERA_primitive_purpose ) continue;

                    const calderaShared = prim.extensions.CALDERA_shared_mesh;
                }
            }

            for( currNode.children ) | childIdx | Pfn( data, childIdx, trs );
        }
    }.Pfn;

    const scene: gltf_scene = glbData.scenes[ glbData.scene.? ];
    for( scene.nodes ) | rootNodeIdx |
    {
        LmbdVisitNode( glbData, rootNodeIdx, .{} );
    }
}


// NOTE: extensions
pub const gltf_caldera_player_samples = struct
{
    time:       u32,
    life:       u32,
    opacity:    u32,
    color:      u32,
};

pub const gltf_prim_purpose = enum { collision, shadow, sky, decal };

pub const gltf_caldera_primitive_purpose = struct
{
    purpose: gltf_prim_purpose,
};

pub const gltf_caldera_shared_mesh = struct
{
    parentFile: []const u8,
    meshIdx:    u32,
    primIdx:    u32,
};

pub const gltf_geom_type = enum { terrain };

pub const gltf_caldera_geom_type = struct
{
    type: gltf_geom_type,
};

pub const gltf_khr_node_visibility = struct
{
    visible: bool = true,
};

pub const gltf_ext_mesh_gpu_instancing = struct
{
    attributes: struct
    {
        TRANSLATION:    ?u32 = null,
        ROTATION:       ?u32 = null,
        SCALE:          ?u32 = null,
    },
};

pub const gltf_msft_lod = struct
{
    ids: []const u32,
};

// NOTE: GLB headers
pub const ZPK_GLB_MAGIC:            u32 = 0x46546C67;
pub const ZPK_GLB_CHUNK_TYPE_JSON:  u32 = 0x4E4F534A;
pub const ZPK_GLB_CHUNK_TYPE_BIN:   u32 = 0x004E4942;

pub const glb_header = extern struct
{
    magic:      u32,
    version:    u32,
    length:     u32,
};

pub const glb_chunk_header = extern struct
{
    length:     u32,
    type:       u32,
};

pub fn GltfParseFromBytes( bytes: []const u8, alloc: std.mem.Allocator ) gltf_data
{
    var cursor: []const u8 = bytes;

    const hdr: glb_header = std.mem.bytesToValue( glb_header, cursor[ 0..@sizeOf( glb_header ) ] );
    if( ZPK_GLB_MAGIC != hdr.magic ) @panic( "Not a glb" );
    cursor = cursor[ @sizeOf( glb_header ).. ];

    const jsonHdr: glb_chunk_header = std.mem.bytesToValue( glb_chunk_header, cursor[ 0..@sizeOf( glb_chunk_header ) ] );
    if( ZPK_GLB_CHUNK_TYPE_JSON != jsonHdr.type ) @panic( "First glb chunk is not JSON" );
    cursor = cursor[ @sizeOf( jsonHdr ).. ];

    const json: []const u8 = cursor[ 0..jsonHdr.length ];
    cursor = cursor[ jsonHdr.length.. ];

    //const binHdr: glb_chunk_header = std.mem.bytesToValue( glb_chunk_header, cursor[ 0..@sizeOf( glb_chunk_header ) ] );
    //if( ZPK_GLB_CHUNK_TYPE_BIN != binHdr.type ) @panic( "Second glb chunk is not BIN" );
    //const bin: []const u8 = cursor[ @sizeOf( binHdr )..( @sizeOf( binHdr ) + binHdr.length ) ];

    return std.json.parseFromSliceLeaky( gltf_data, alloc, json, .{ .ignore_unknown_fields = true } )
        catch @panic( "Failed to parse glb json" );
}