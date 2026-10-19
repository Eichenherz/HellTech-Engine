const std = @import( "std" );
const zpk = @import( "ZpkGltf.zig" );

var g_jobsCounter: std.atomic.Value( u64 ) = .init( 0 );

var g_io: std.Io = undefined;

threadlocal var g_threadArenas: [ 2 ]std.heap.ArenaAllocator = .{
    .init( std.heap.smp_allocator ),
    .init( std.heap.smp_allocator )
};

fn ZHpkMMapFile( hFile: std.Io.File ) std.Io.File.MemoryMap
{
    const len: u64        = hFile.length( g_io ) catch @panic( "Failed to get file len !!!" );
    const mm = hFile.createMemoryMap( g_io, .{
        .len                    = len,
        .protection = .{ .read = true }
    } ) catch @panic( "Failed to mmap call !!!!" );

    if( null == mm.section ) @panic( "OS mapping failed !!!!" );

    return mm;
}

fn HpkProcessMeshesParallelJob( glbs: []const []const u8 ) void
{
    while( true )
    {
        const jobIdx: u64 = g_jobsCounter.fetchAdd( 1, .monotonic );
        if( jobIdx >= glbs.len ) break;

        const hFile = std.Io.Dir.cwd().openFile( g_io, glbs[ jobIdx ], .{} )
            catch @panic( "Failed to open file" );
        defer hFile.close( g_io );

        var mm = ZHpkMMapFile( hFile );
        defer mm.destroy( g_io );

        const gltfData: zpk.gltf_data = zpk.GltfParseFromBytes( mm.memory, g_threadArenas[ 0 ].allocator() );

        for( &g_threadArenas ) | *pArena | { _ = pArena.reset( .retain_capacity ); }
    }
}

pub fn main( proc: std.process.Init ) void
{
    g_io = proc.io;

    const args = proc.minimal.args.toSlice( proc.arena.allocator() )
        catch @panic( "Failed to read args" );
    if( args.len < 4 ) @panic("Missing arguments");

    var dirStr: []const u8 = "";
    for( 1..args.len - 1 ) | i |
    {
        if( std.mem.eql( u8, args[ i ], "--dir" ) )
        {
            dirStr = args[ i + 1 ];
            break;
        }
    }
    const dir = std.Io.Dir.cwd().openDir( g_io, dirStr, .{ .iterate = true } )
        catch @panic( "Missing or invalid --dir" );

    var tmpBuff: [ 128 ][]const u8 = undefined;
    var glbs: std.ArrayList( []const u8 ) = .initBuffer( &tmpBuff );

    var it = dir.iterate();
    while( it.next( g_io ) catch null ) | entry |
    {
        if( std.mem.endsWith( u8, entry.name, ".glb" ) )
        {
            const name = std.fs.path.join( proc.arena.allocator(), &.{ dirStr, entry.name } )
                catch @panic( "Out of memory" );
            glbs.appendBounded( name ) catch @panic( "More than 128 .glb files" );
        }
    }
    if( 0 == glbs.items.len ) std.debug.panic( "No .glbs in dir: {s}", .{ dirStr } );

    const threadCount: u64 = ( std.Thread.getCpuCount() catch 1 ) / 2;
    const threads = proc.arena.allocator().alloc( std.Thread, threadCount )
        catch @panic( "Out of memory" );

    for( threads ) | *t |
    {
        t.* = std.Thread.spawn( .{}, HpkProcessMeshesParallelJob, .{ glbs.items } )
            catch @panic( "Failed to spawn thread" );
    }
    for( threads ) | t | t.join();

    const hpkOut: [:0] const u8 = args[ 3 ];

    _ = hpkOut;
}