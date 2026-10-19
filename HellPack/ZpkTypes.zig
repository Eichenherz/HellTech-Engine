const zm = @import( "ZHtMath" );

pub const trs_t = struct
{
    t: zm.float3    = .{ 0, 0, 0 },
    r: zm.quat4     = .{ 0, 0, 0, 1 },
    s: zm.float3    = .{ 1, 1, 1 },

    pub fn TrsCompose( self: trs_t, other: trs_t ) trs_t
    {
        return .{
            .t = other.t + zm.VecRot( self.t * other.s, other.r ),
            .r = zm.QuatMul( self.r, other.r ),
            .s = self.s * other.s
        };
    }
};

pub fn Mat4x4ToTrs( m: [ 16 ]f32 ) trs_t
{
    const canonical = [ 3 ]zm.float3{ .{ 1, 0, 0 }, .{ 0, 1, 0 }, .{ 0, 0, 1 } };
    const eps = 0.0001;

    var basis = @as( *align( 4 ) const [ 3 ]zm.float3, @ptrCast( &m ) ).*;
    var s = [ 3 ]f32{ zm.Length( basis[ 0 ] ), zm.Length( basis[ 1 ] ), zm.Length( basis[ 2 ] ) };
    const a, const b, const c = zm.Rank3( s );

    if( s[ a ] < eps ) basis[ a ] = canonical[ a ];
    basis[ a ] = zm.Normalize( basis[ a ] );

    if( s[ b ] < eps )
    {
        _, _, const cc = zm.Rank3( @abs( basis[ a ] ) );
        basis[ b ] = zm.Cross( basis[ a ], canonical[ cc ] );
    }
    basis[ b ] = zm.Normalize( basis[ b ] );

    if( s[ c ] < eps ) basis[ c ] = zm.Cross( basis[ a ], basis[ b ] );
    basis[ c ] = zm.Normalize( basis[ c ] );

    var det = zm.Dot( zm.Cross( basis[ 0 ], basis[ 1 ] ), basis[ 2 ] );
    if( det < 0 )
    {
        s[ a ] = -s[ a ];
        basis[ a ] = -basis[ a ];
        det = -det;
    }
    if( eps < ( det - 1 ) * ( det - 1 ) ) return .{};

    return .{ .t = m[ 12..15 ].*, .r = zm.QuatFromRotMat( basis ), .s = s };
}


const packed_trs = extern struct
{
    t: [ 3 ]f32,
    r: [ 4 ]f32,
    s: [ 3 ]f32,
};