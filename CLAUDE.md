# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

HellTech Engine — a WIP real-time renderer on Windows using Vulkan 1.2+ (primary) and DX12. Features meshlet-based GPU culling, bindless resources / BDAs, HiZ occlusion, BCn texture compression, and a custom binary asset format (HellPack).

## Build

**Primary tool: Visual Studio 2022** (`HellTech.sln`). There is no CMake or command-line build script.

Build configurations:
- `Debug|x64` — Vulkan path, full debug info (`_DEBUG`, `_VK_DEBUG_`)
- `Release|x64` — DX12 path, whole-program optimization
- `VK_Debug|x64` — Vulkan path, optimized but with debug info

Output goes to `bin\x64\<Config>\`, intermediates to `int\<Project>\x64\<Config>\`.

### Setting up dependencies

Run once before opening the solution:
```bat
get_3rdparty_deps.bat
```
This clones all git deps into `3rdParty/` and downloads miniz. NuGet packages (DXC, D3D12 Agility SDK) in `packages/` are restored automatically by VS.

### Compiling HLSL shaders

```bat
run_dxc_compile.bat
```
Calls `dxc_compile_shaders.py`. Requires the `DXC` environment variable to point to a DXC executable (the script also accepts `--src`, `--out`, `--sm`, `--vk` args). GLSL shaders are compiled to SPIR-V automatically during VS build via a custom build step that invokes `glslangValidator` (requires `VULKAN_SDK` environment variable).

## Architecture

### Projects

| Project | Description |
|---------|-------------|
| `Engine/` | Main renderer executable |
| `HellPack/` | Standalone asset compiler (GLTF → `.hellpack`) |
| `Lib/` | Shared headers only (`.vcxitems` — included into both projects) |

### Lib/ — Shared foundation

Headers shared across Engine and HellPack:
- `core_types.h` — primitive type aliases (`u8`/`u16`/`u32`/`u64`, `i8`…`i64`)
- `hell_pack.h` — HellPack binary format definition (`hellpack_file_header`, `hellpack_level`, etc.)
- `ht_gfx_types.h` — GPU-facing types shared between CPU and shaders
- `ht_math.h`, `ht_vec_types.h` — math primitives
- `ht_mem_arena.h`, `ht_error.h`, `ht_utils.h`, `range_utils.h` — utilities

### Engine/ — Renderer

The renderer has an abstract `renderer_interface` (in `sys_os_api.h`) with `InitBackend`, `UploadAsync`, and `HostFrames`. The Vulkan implementation lives in `vk_backend.cpp`, the DX12 stub in `r_dx12_backend.cpp` (not compiled by default in Debug).

Key files:
- `renderer.cpp` — top-level render loop; includes `r_data_structs.h` after Vulkan headers
- `r_data_structs.h` — **dual-use CPU/GPU header**: compiled as C++ by the engine and as GLSL (via `#ifdef __cplusplus` guards); defines `frame_data`, math type aliases, and GLSL extensions
- `sys_os_api.h` — platform/engine interface boundary; defines `renderer_interface` and platform callbacks (`SysTicks`, `SysReadFile`, etc.)
- `sys_os_win.cpp` — Windows platform layer (Win32 window, input, file I/O)
- `vk_context.h` — Vulkan device context; queues (GFX/COPY/COMP), command pool/buffer management, VMA allocator, descriptor pool
- `vk_resources.h`, `vk_pso.h`, `vk_sync.h` — Vulkan resource/PSO/sync abstractions
- `dxc.h/.cpp` — runtime DXC shader compilation (HLSL → SPIR-V at runtime)

The `_VK_DEBUG_` preprocessor define selects the Vulkan code path.

### HellPack/ — Asset Compiler

Standalone tool that ingests GLTF files and emits `.hellpack` binary bundles. Pipeline:
1. `gltf_loader.h` — loads GLTF via cgltf/tinygltf into `raw_mesh` / `raw_image_view`
2. `mikkt_space.h` — computes MikkTSpace tangent frames (3-byte packed)
3. `hp_encoding.h` — vertex attribute encoding/quantization
4. `hp_bcn_compression.h` — BCn texture compression via bc7enc_rdo
5. `hp_serialization.h` — serializes everything into the HellPack binary format
6. `HellPack.cpp` — entry point; orchestrates the pipeline using multithreading

### Shaders/

Mixed GLSL (`.glsl`) and HLSL (`.hlsl`) shaders:
- GLSL compiled to `.spv` by glslangValidator during VS build
- HLSL compiled to `.spirv` by `dxc_compile_shaders.py` → `bin/SpirV/`
- `r_data_structs.h` and `Shaders/r_data_structs2.h` are `#include`d directly in GLSL shaders for shared struct definitions
- Shader naming conventions: `v_` = vertex, `f_` = fragment, `c_` = compute

## Key Conventions

- No precompiled headers (`<PrecompiledHeader>NotUsing</PrecompiledHeader>`)
- C++20 / C17; AVX2 intrinsics enabled (`/arch:AVX2`)
- EASTL used alongside STL (custom `eastl_new.cpp` provides allocator hook)
- Invalid indices represented as `~T{}` (all-bits-set); use `IsIndexValid()` from `core_types.h`
- `DEFS_WIN32_NO_BS.h` must be included before `<Windows.h>` to strip bloat macros
- Reversed-Z depth (clear depth to 0, near plane maps to 1)
