# HellTech Codebase Map

Quick-reference for key structs, data flows, and gotchas discovered during debugging.
CLAUDE.md has the high-level architecture; this has the low-level detail.

---

## Critical struct sizes (RenderDoc / buffer math)

| Struct | Size | Layout |
|---|---|---|
| `packed_trs` | **48 B** | float3 t (12) + float pad0 (4) + float4 r (16) + float3 s (12) + float pad1 (4) |
| `gpu_instance` | **56 B** | packed_trs (48) + u32 meshIdx (4) + u32 mtrlIdx (4) |
| `visible_meshlet` | **60 B** | packed_trs (48) + u32 absVtxOffset (4) + u32 absTriOffset (4) + u16 vtxCount (2) + u16 triCount (2) |
| `world_node` | **64 B** | packed_trs (48) + u64 meshHash (8) + u16 materialIdx (2) + 6B tail pad |

**RenderDoc gotcha**: both GPU buffers (`gpuInstances`, `visibleClusters`) store the larger wrapper structs, NOT bare `packed_trs`. Using 48-byte stride in RenderDoc gives phantom data in pad0/pad1 for every entry past index 0.

---

## World basis (RH, Y-up, GLTF convention)
```
FWD  = { 0,  0, -1 }   // -Z forward
LEFT = {-1,  0,  0 }
UP   = { 0,  1,  0 }
```
Defined as globals in `Engine/HellTech.cpp`. Camera uses `XMMatrixRotationRollPitchYaw(pitch, yaw, 0)` with DirectXMath (which is LH-native). With FWD=-Z, mouse deltas must be **negated** before adding to yaw/pitch — positive mouseDx with FWD=-Z rotates camera LEFT, not right.

---

## TRS composition (`HellPack/gltf_loader.h` — `GltfComposePackedTRS`)

Standard formula: `world = parent * child` (GLTF column-major convention).
```
outS = parentS * childS
outR = XMQuaternionMultiply(childR, parentR)      // child FIRST — XMQuatMul(Q1,Q2) = Q1 applied first
outT = parentT + XMVector3InverseRotate(childT * parentS, parentR)
```
Key: `XMQuaternionMultiply(Q1, Q2)` means "apply Q1 first, then Q2". Correct order is **child before parent**.

**Quaternion convention gotcha**: GLTF uses `q*v*q^{-1}` (standard rotation = `XMVector3InverseRotate`). DirectXMath's `XMVector3Rotate` computes the opposite: `conj(q)*v*q`. Using the wrong one mirrors child translations across rotated parent frames. Always use `XMVector3InverseRotate` when rotating GLTF-sourced translations.

`IDENTITY_TRS` is defined in `Lib/ht_math.h`. `GltfComposePackedTRS` lives in `HellPack/gltf_loader.h` (GLTF-specific, not shared into Engine).

---

## GLTF → GPU data flow

```
tinygltf::Node
  └─ GetTrsFromNode()          → packed_trs (local)
       └─ XMComposePackedTRS() → packed_trs (world)  [HellPack/gltf_loader.h]
            └─ raw_node { packed_trs toWorld; i32 meshIdx }
                 └─ HellPack.cpp loop → world_node { packed_trs, u64 meshHash, u16 materialIdx }
                      └─ hellpack binary (world.lvl entry 0)
                           └─ HpkReadBinaryBlob<hellpack_level>() → typed_view<world_node>
                                └─ Engine: instance_desc { packed_trs transform; HRNDMESH32 meshIdx; u16 materialIdx }
                                     └─ gpu_instance { packed_trs toWorld; u32 meshIdx; u32 mtrlIdx }
                                          └─ HOST_VISIBLE gpuInstances buffer (per frame-in-flight)
```

---

## GLTF node traversal (`HellPack/gltf_loader.h — ProcessNodes`)

- Starts from `model.scenes[0].nodes` (scene root list), NOT all nodes by index
- DFS via `std::vector<__gltf_node>` used as stack (push_back/pop_back)
- `__gltf_node` = `{ fixed_string<256> name; packed_trs parentTRS; i32 nodeIdx }`
- Children pushed with `{ .parentTRS = worldTRS, .nodeIdx = childIdx }` (use designated init — positional init is wrong if name is first field)

---

## Matrix convention

**Convention: M × v (column-vector). Row-major matrix storage — no pre-transposing.**

- CPU stores DirectXMath matrices as-is (row-major, no transpose). `GetViewData()` in `HellTech.cpp`.
- HLSL call sites use `mul(M, v)` — matrix on the left, column vector on the right.
- MVP is built as `mul(proj, mul(view, world))` = P×V×W, applied as `mul(mvp, pos)`.
- `TrsToFloat4x4` in `ht_hlsl_math.h` outputs column-vector layout: translation in last **column** (row 3 = [0,0,0,1]).
- `FrustumCulling` in `culling.h` still uses `transpose(mvp)` internally for plane extraction — intentional, left as-is.
- Do NOT add `transpose()` calls or flip `mul` argument order without updating everything consistently.

## Shaders that affect drawing / use matrices

| Shader | Role | Matrix usage |
|---|---|---|
| `v_vbuffer.hlsl` | Vertex — clip-space transform | `TrsToFloat4x4`, `mul(v, mvp)` |
| `c_lambertian_clay.hlsl` | Compute shading — world pos/normals | `TrsToFloat4x4`, `mul(clip, mvp)`, `QuatRot` |
| `c_draw_cull.hlsl` | Compute culling | Loads `view_data`; MVP currently commented out |
| `c_expand_draws.comp.hlsl` | Instance→meshlet expansion | Copies `packed_trs toWorld`, no matrix math |
| `c_meshlet_issue_draws.comp.hlsl` | Emits indirect draw commands | No matrix math |

## GPU render pipeline (culling → shading)

```
gpu_instance buffer
  └─ c_meshlet_culling.comp    → compactedInst (visible_instance list)
       └─ c_expand_draws.comp  → visible_meshlet buffer  (copies inst.toWorld + per-meshlet offsets)
            ├─ v_vbuffer.vert  → vbuffer (triIdx | mltIdx per pixel)
            └─ c_lambertian_clay.comp  → reads visible_meshlet by mltIdx, reconstructs world pos
```

`visible_instance` = `{ u32 instId; u32 meshletOffset; u32 meshletCount; u32 vtxOffset; u32 triOffset }` (20 bytes, GPU-only)

---

## Shader struct sharing

`Lib/ht_renderer_types.h` is `#include`d directly in HLSL shaders (via `Shaders/ht_hlsl_lang.h`).
Struct layout is identical between C++ and SPIR-V std430 because:
- `float3` = 12 bytes, base alignment 16 — satisfied by explicit `pad0`/`pad1` fields
- All `float4` and `float3+float` pairs land on 16-byte boundaries

No `r_data_structs.h` / `r_data_structs2.h` exists for HLSL — `ht_renderer_types.h` is the one true shared header.

---

## Key file locations

| What | Where |
|---|---|
| `packed_trs` definition + static_assert | `Lib/ht_renderer_types.h:54` |
| `GltfComposePackedTRS` | `HellPack/gltf_loader.h` (before `GetTrsFromNode`) |
| `IDENTITY_TRS` | `Lib/ht_math.h:358` |
| `world_node`, `material_desc` | `Lib/ht_gfx_types.h` |
| `gpu_instance`, `visible_meshlet`, `gpu_mesh` | `Lib/ht_renderer_types.h:90,200` |
| GLTF loader + node traversal | `HellPack/gltf_loader.h` |
| HellPack serialization loop | `HellPack/HellPack.cpp:527` |
| Camera + input | `Engine/HellTech.cpp:38` |
| Instance buffer upload | `Engine/renderer.cpp:1491` |
| Expand draws shader | `Shaders/c_expand_draws.comp.hlsl` |
| Shading | `Shaders/c_lambertian_clay.hlsl` |
| VBuffer pass | `Shaders/v_vbuffer.hlsl` |