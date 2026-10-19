pub const float3    = @Vector( 3, f32 );
pub const quat4     = @Vector( 4, f32 );

const shfl_xyz = [ 3 ]i32{ 0, 1, 2 };
const shfl_yzx = [ 3 ]i32{ 1, 2, 0 };
const shfl_zxy = [ 3 ]i32{ 2, 0, 1 };

pub fn Dot( a: float3, b: float3 ) f32 { return @reduce( .Add, a * b ); }

pub fn Cross( a: float3, b: float3 ) float3
{
    return @shuffle( f32, a, undefined, shfl_yzx ) * @shuffle( f32, b, undefined, shfl_zxy ) -
        @shuffle( f32, a, undefined, shfl_zxy ) * @shuffle( f32, b, undefined, shfl_yzx );
}

pub fn QuatMul( q1: quat4, q2: quat4 ) quat4
{
    const q1xyz: float3 = @shuffle( f32, q1, undefined, shfl_xyz );
    const q2xyz: float3 = @shuffle( f32, q2, undefined, shfl_xyz );
    const xyz: float3   = @as( float3, @splat( q2[ 3 ] ) ) * q1xyz +
        @as( float3, @splat( q1[ 3 ] ) ) * q2xyz + Cross( q2xyz, q1xyz );
    return .{ xyz[ 0 ], xyz[ 1 ], xyz[ 2 ], q2[ 3 ] * q1[ 3 ] - Dot( q2xyz, q1xyz ) };
}

pub fn VecRot( v: float3, q: quat4 ) float3
{
    const qxyz: float3  = @shuffle( f32, q, undefined, shfl_xyz );
    const t: float3     = @as( float3, @splat( 2.0 ) ) * Cross( qxyz, v );
    return v + @as( float3, @splat( q[ 3 ] ) ) * t + Cross( qxyz, t );
}

pub fn Length( v: float3 ) f32 { return @sqrt( Dot( v, v ) ); }

pub fn Normalize( v: float3 ) float3 { return v / @as( float3, @splat( Length( v ) ) ); }

pub fn Rank3( v: [ 3 ]f32 ) [ 3 ]u64
{
    if( v[ 0 ] < v[ 1 ] )
    {
        if( v[ 1 ] < v[ 2 ] ) return .{ 2, 1, 0 };
        if( v[ 0 ] < v[ 2 ] ) return .{ 1, 2, 0 };
        return .{ 1, 0, 2 };
    }
    if( v[ 0 ] < v[ 2 ] ) return .{ 2, 0, 1 };
    if( v[ 1 ] < v[ 2 ] ) return .{ 0, 2, 1 };
    return .{ 0, 1, 2 };
}

pub fn QuatFromRotMat( m: [ 3 ]float3 ) quat4
{
    if( m[ 2 ][ 2 ] <= 0 )
    {
        const dif10 = m[ 1 ][ 1 ] - m[ 0 ][ 0 ];
        const omr22 = 1 - m[ 2 ][ 2 ];
        if( dif10 <= 0 )
        {
            const fourXSqr = omr22 - dif10;
            const inv4x = 0.5 / @sqrt( fourXSqr );
            return .{
                fourXSqr * inv4x,
                ( m[ 0 ][ 1 ] + m[ 1 ][ 0 ] ) * inv4x,
                ( m[ 0 ][ 2 ] + m[ 2 ][ 0 ] ) * inv4x,
                ( m[ 1 ][ 2 ] - m[ 2 ][ 1 ] ) * inv4x
            };
        }
        const fourYSqr = omr22 + dif10;
        const inv4y = 0.5 / @sqrt( fourYSqr );
        return .{
            ( m[ 0 ][ 1 ] + m[ 1 ][ 0 ] ) * inv4y,
            fourYSqr * inv4y,
            ( m[ 1 ][ 2 ] + m[ 2 ][ 1 ] ) * inv4y,
            ( m[ 2 ][ 0 ] - m[ 0 ][ 2 ] ) * inv4y
        };
    }
    const sum10 = m[ 1 ][ 1 ] + m[ 0 ][ 0 ];
    const opr22 = 1 + m[ 2 ][ 2 ];
    if( sum10 <= 0 )
    {
        const fourZSqr = opr22 - sum10;
        const inv4z = 0.5 / @sqrt( fourZSqr );
        return .{
            ( m[ 0 ][ 2 ] + m[ 2 ][ 0 ] ) * inv4z,
            ( m[ 1 ][ 2 ] + m[ 2 ][ 1 ] ) * inv4z,
            fourZSqr * inv4z,
            ( m[ 0 ][ 1 ] - m[ 1 ][ 0 ] ) * inv4z
        };
    }
    const fourWSqr = opr22 + sum10;
    const inv4w = 0.5 / @sqrt( fourWSqr );
    return .{
        ( m[ 1 ][ 2 ] - m[ 2 ][ 1 ] ) * inv4w,
        ( m[ 2 ][ 0 ] - m[ 0 ][ 2 ] ) * inv4w,
        ( m[ 0 ][ 1 ] - m[ 1 ][ 0 ] ) * inv4w,
        fourWSqr * inv4w
    };
}