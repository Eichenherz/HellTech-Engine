const std = @import( "std" );

pub fn build( b: *std.Build ) void
{
    const target = b.standardTargetOptions( .{} );
    const optimize = b.standardOptimizeOption( .{} );

    const cfgDir = switch( optimize )
    {
        .Debug => "Debug",
        .ReleaseSafe => "RelWithDebInfo",
        .ReleaseFast, .ReleaseSmall => "Release",
    };

    const zht_math = b.createModule( .{
        .root_source_file = b.path( "HtLib/ZHtMath.zig" ),
        .target = target,
        .optimize = optimize,
    } );

    const zhpk = b.addExecutable( .{
        .name = "ZHellPack",
        .root_module = b.createModule( .{
            .root_source_file = b.path( "HellPack/ZHellPack.zig" ),
            .target = target,
            .optimize = optimize,
            .dwarf_format = .@"32",
            .imports = &.{ .{ .name = "ZHtMath", .module = zht_math } },
        } ),
    } );

    const install = b.addInstallArtifact( zhpk, .{
        .dest_dir = .{ .override = .{ .custom = b.fmt( "../bin/{s}", .{ cfgDir } ) } },
    } );
    b.getInstallStep().dependOn( &install.step );
}