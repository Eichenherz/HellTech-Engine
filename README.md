# HellTech Engine
( Another ) Handmade ( mostly ) Game Engine. Made for fun with hellish passion I guess.


### REQUIREMENTS:
- Win10+
- Vulkan1.4+
- dedicated GPU ( preferably NV )


### HtLib SUBTREE :
- Remote: https://github.com/Eichenherz/HtLib.git → `HtLib/`
- Pull upstream changes: `git htlib-pull`
- Push local changes back: `git htlib-push`
- Aliases are local to this repo (`.git/config`)


### DEPENDENCIES :
- [meshoptimizer](https://github.com/zeux/meshoptimizer)
- [cgltf](https://github.com/jkuhlmann/cgltf)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog)
- [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [bc7enc_rdo](https://github.com/Eichenherz/bc7enc_rdo)
- [miniz](https://github.com/richgel999/miniz)
- [unordered_dense](https://github.com/martinus/unordered_dense)
- [dds](https://github.com/turanszkij/dds)
- [OffsetAllocator](https://github.com/sebbbi/OffsetAllocator)
- [minunit](https://github.com/kattkieru/minunit)
- DirectXShaderCompiler ( comes with Win10SDK )

### FEATURES ( WIP ):
- Descriptor Indexing + BDAs for all ( bindless design )
- Frustum and Double pass HZB Occlusion culling ( Instance and Meshlets )
- Async GPU uploads
- Custom binary file format ( hpk - HellPack ) based on ZIP ( Quake 3 style )
- Asset compiler and exporter ( gltf -> hpk only ): features mesh optimization and tex compression
- Custom memory and job system

### POINTS OF INTEREST:
----------------------------------
#### Rendering Backend — `Engine/`
| File | What it does |
|------|-------------|
| `renderer.cpp` | Top-level render loop: culling, HZB, forward pass, ImGui, GPU profiler |
| `vk_backend.cpp` | Vulkan device init, swapchain, queues, async copy queue for texture/mesh uploads |
| `vk_context.h` | Device context: queues, command pools, VMA allocator, timestamp + pipeline-stats query pools |
| `vk_resources.h` | Typed wrappers for buffers, images, samplers, descriptor sets |
| `vk_sync.h` | Timeline semaphores, pipeline barriers |


#### GPU Culling Pipeline — `Shaders/`
| Stage | What it does                                                                                                          |
|-------|-----------------------------------------------------------------------------------------------------------------------|
| `c_draw_cull.hlsl` | Per-instance frustum + HZB occlusion cull (early + late pass); writes surviving instances into a GPU-side draw buffer |
| `c_meshlet_cull.hlsl` | Per-meshlet cone + HZB cull on surviving instances                                                                    |
| `v_/f_vbuffer.hlsl` | Visibility buffer pass: rasterizes meshlets                                                                           |
| `vbuffer.h` | Shared structs + pack/unpack helpers for the visibility buffer pixel format                                           |
| `c_lambertian_clay.hlsl` | Reads the vbuffer, reconstructs geometry, shades with lambertian clay (debug/reference mode)                          |
| `ht_hlsl_lang.h` | HLSL utility macros (NOINTERP, NUMTHREADS, etc.) and SPIR-V capability constants shared across all shaders            |


#### Asset Pipeline — `HellPack/`
| File | What it does |
|------|-------------|
| `HellPack.cpp` | Entry point; multi-threaded orchestration of the GLTF → HellPack pipeline |
| `gltf_loader.h` | Ingests GLTF via cgltf into raw mesh/image views |
| `hp_encoding.h` | Vertex quantization and meshlet generation (via meshoptimizer) |
| `hp_bcn_compression.h` | BCn (BC1/BC7) GPU texture compression via bc7enc_rdo |
| `hp_serialization.h` | Writes the final `.hellpack` binary bundle (ZIP-based, Quake 3 style) |

#### Platform Layer — `Engine/`
| File | What it does                                                                        |
|------|-------------------------------------------------------------------------------------|
| `sys_os_win.cpp` | Win32 window, Raw Input pump, file I/O, main loop                                   |
| `engine_platform_common.h` | Platform↔engine interface: `renderer_interface`, `ht_input_state`, `job_system_ctx` |
| `HellTech.cpp` | Game-side logic: camera, input bindings, UI assembly, frame loop                    |

#### Shared Foundation — `Lib/`
| File | What it does |
|------|-------------|
| `ht_gfx_types.h` | GPU-facing structs shared between C++ and GLSL shaders |
| `ht_mem_arena.h` | Virtual arena allocator (no heap fragmentation, O(1) alloc) |
| `hell_pack.h` | HellPack binary format definition |


![HellTech Engine 8_27_2021 12_35_42 PM](https://user-images.githubusercontent.com/32171756/135079403-c1c025b4-bb22-4181-a33a-0a49b469a5e6.png)
![HellTech Engine 9_2_2021 1_43_40 PM](https://user-images.githubusercontent.com/32171756/135079505-5b91c42c-8445-46d4-b7e2-c3f41124a4a9.png)
![HellTech Engine 10_3_2021 1_15_57 PM](https://user-images.githubusercontent.com/32171756/135749331-4a191c8f-d44b-473b-baba-4361418860cc.png)


