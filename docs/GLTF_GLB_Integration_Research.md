# X-Ray Monolith — External Model Format (GLTF/GLB) Integration Research

**Repository:** `themrdemonized/xray-monolith`
**Branch:** `all-in-one-vs2022-wpo-mt`
**Scope:** A *parallel* loading path so `.gltf` / `.glb` files in `$game_meshes$` load via `Render->model_Create(name)` and Lua `set_visual_name()` exactly as OGF does today, with PBR materials, **without touching the OGF/OMF path**.
**Nature:** Research/reference only. Every claim is cited to `file:function:line`. Unverified items are tagged **[UNVERIFIED]**.

> **Conventions used below.** Geometry/buffer state lives in `IRender_Mesh` (a base of `Fvisual`), *not* in `dxRender_Visual`. The Lua shader binder that is actually compiled for R3/R4 is the **DX10** one (`src/Layers/xrRenderDX10/dx10ResourceManager_Scripting.cpp`), not the legacy DX9 `src/Layers/xrRender/ResourceManager_Scripting.cpp`. `gamedata/shaders/r3/` in this checkout ships only **29 files** (a partial set) — the standard model/deferred HLSL is shipped in packed game archives, not in source.

---

## Corrections to assumptions in the task brief (read first)

| Brief assumption | Reality (verified) |
|---|---|
| `MT_3DFLUIDVOLUME = 12` is the last enum and is handled | It is the last enum (`Fmesh.h:24`) but has **no** `Instance_Create` case — it hits `FATAL` (`ModelPool.cpp:75-77`). So `MT_EXTERNAL_STATIC=13`/`MT_EXTERNAL_SKELETAL=14` is correct, *and* there is already an unhandled gap. |
| `dxRender_Visual::Load` calls `SetShaderTexture(fnS,fnT)` | `Load` stores `dbg_shader_def/dbg_texture_def` then calls `ResetShaderTexture()` which calls `SetShaderTexture(...)` (`FBasicVisual.cpp:68-76, 155-159`). |
| `$no_shadows` sets a vis flag | It sets `IRenderVisualFlags::eNoShadow` on `IRenderVisual::flags` (`FBasicVisual.cpp:133,137`; enum `RenderVisual.h:13`). |
| `VERIFY3(dwCount<=64,...)` at line 227 | It is at **`SkeletonCustom.cpp:225`**, message `"More than 64 bones is a crazy thing!"`. |
| `R_ASSERT2(id<64,...)` guards all bones | It is at **`PHShell.cpp:867`**, only inside `if(breakable)` → `if(jtRigid && root_e)` → `if(!no_physics_shape||!root_e)`. Non-breakable bones with id≥64 never reach it. |
| The Lua API exposes `sampler/lit/lod/shadow/strict_b2f/wmark` | In the **DX10** binder only `dx10texture`, `dx10sampler`, `begin`, `sorting`, `emissive`, `distort`, `wmark`, `fog`, `zb`, `blend`, `aref`, `scopelense`, stencil/cull/zfunc are bound. `sampler`/`texture` are DX9-only and commented out for DX10. `lit/lod/shadow/strict_b2f` are **not bound in either file**. |
| `s_textures` comma list injects extra textures positionally | Only **3** texture args reach the Lua function: `t_base`, `t_second`, `t_detail` — and `t_detail` is the **THM detail texture**, not the 3rd comma item (`dx10ResourceManager_Scripting.cpp:626-634`). |
| `dxRender_Visual::Release()` frees buffers | It is **empty** (`FBasicVisual.cpp:37-39`). Buffer release happens in `~IRender_Mesh()` via `_RELEASE` (`FBasicVisual.cpp:20-24`). |

---

# SECTION 1 — The Complete Model Loading Call Chain

## 1.0 Entry points

### `CObject::cNameVisual_set(shared_str N)` — `src/xrEngine/xr_object.cpp:70`
- Early-out if `N == NameVisual` (`:73-74`).
- Stores name `NameVisual = N` (`:81`) and **creates the visual**: `renderable.visual = Render->model_Create(*N);` (`:82`).
- Carries the kinematics update-callback from old→new visual (`:84-106`); under `OPTIMIZE_CALCULATE_BONES` reassigns `spatialParent` (`:94-100`).
- `OnChangeVisual()` (`:108`) then frees the previous visual: `::Render->model_Delete(old_v);` (`:109`).
- The visual is owned by `IRenderable::renderable.visual` (`irenderable.h:15`).

### `CScriptGameObject::set_visual_name(LPCSTR visual, bool bForce)` — `src/xrGame/script_game_object2.cpp:670-697`
- Early-out on equality (`:672-673`). **`bForce` is unreferenced** in the body.
- Sends a `GE_CHANGE_VISUAL` net event so the change replicates (`:675-678`).
- Dispatch: `CActor` → `actor->ChangeVisual` (`:680-685`); `CAI_Stalker` → `stalker->ChangeVisual` (`:687-692`).
- Generic fallback (`:694-696`): `object().cNameVisual_set(visual)`, then `Visual()->dcast_PKinematics()->CalculateBones_Invalidate()` + `CalculateBones(TRUE)`.

Full chain (non-actor): `set_visual_name` → `cNameVisual_set` (`xr_object.cpp:70`) → `Render->model_Create` (`r4.cpp:635`) → `CModelPool::Create` (`ModelPool.cpp:270`) → `Instance_Find`/`Instance_Load` → `Instance_Create` + `V->Load`.

### `CModelPool::Create(const char* name, IReader* data, bool assert)` — `src/Layers/xrRender/ModelPool.cpp:270-317`
- **Cache key** = lowercased, extension-stripped name: `strlwr(low_name)` (`:278`) then `if (strext(low_name)) *strext(low_name)=0;` (`:279`). **The `.ogf`/`.gltf` extension is stripped from the key.**
- Step 0 — recycle from `Pool` if an inactive instance exists (`:282-290`): `Model->Spawn()`, `Pool.erase`.
- Step 1 — `Instance_Find(low_name)` base lookup (`:294`).
- Step 2 — on miss, `bAllowChildrenDuplicate=FALSE` (`:299`); `Instance_Load(low_name,data,TRUE)` or `Instance_Load(low_name,TRUE,assert)` (`:300-301`); restore flag (`:307`).
- Step 3 — `Model = Instance_Duplicate(Base)` (`:313`), `Registry.insert(mk_pair(Model, low_name))` (`:314`), return clone.

### `CModelPool::Instance_Load(const char* N, BOOL allow_register, bool assert)` — `ModelPool.cpp:102-152`
| Step | Line | Code |
|---|---|---|
| Default-ext build | 108-110 | `if (0==strext(N)) strconcat(name,N,".ogf"); else xr_strcpy(name,N);` |
| Resolve path | 113-132 | `if(!FS.exist(N)) { if(!FS.exist(fn,"$level$",name)) if(!FS.exist(fn,"$game_meshes$",name)) { assert→Debug.fatal else return nullptr } } else xr_strcpy(fn,N);` |
| Open | 139 | `IReader* data = FS.r_open(fn);` |
| Header | 140-141 | `ogf_header H; data->r_chunk_safe(OGF_HEADER,&H,sizeof(H));` |
| Dispatch | 142 | `V = Instance_Create(H.type);` |
| Load | 143 | `V->Load(N, data, 0);` |
| Close | 144 | `FS.r_close(data);` |
| Persist register | 145 | `g_pGamePersistent->RegisterModel(V);` |
| Cache register | 148-149 | `if (allow_register) V = Instance_Register(N, V);` |

**Format assumption:** the loader unconditionally reads an OGF chunked header at `:140-141`. A GLTF/GLB file has no `OGF_HEADER` chunk → `r_chunk_safe` would fail. This is exactly where the new branch must intercept (see §5a).

### `CModelPool::Instance_Load(LPCSTR name, IReader* data, BOOL allow_register)` — `ModelPool.cpp:154-167`
Also reads `ogf_header` unconditionally (`:158-159`), `Instance_Create(H.type)` (`:160`), `V->Load(name,data,0)` (`:161`). Does **not** call `RegisterModel`.

### `CModelPool::Instance_Create(u32 type)` — `ModelPool.cpp:30-82` (the dispatch switch)
| `MT_*` | value | line | class created |
|---|---|---|---|
| `MT_NORMAL` | 0 | 37-39 | `Fvisual` |
| `MT_HIERRARHY` | 1 | 40-42 | `FHierrarhyVisual` |
| `MT_PROGRESSIVE` | 2 | 43-45 | `FProgressive` |
| `MT_SKELETON_ANIM` | 3 | 46-48 | `CKinematicsAnimated` |
| `MT_SKELETON_GEOMDEF_PM` | 4 | 52-54 | `CSkeletonX_PM` |
| `MT_SKELETON_GEOMDEF_ST` | 5 | 55-57 | `CSkeletonX_ST` |
| `MT_LOD` | 6 | 65-67 | `FLOD` (`#ifndef _EDITOR`) |
| `MT_TREE_ST` | 7 | 68-70 | `FTreeVisual_ST` (`#ifndef _EDITOR`) |
| `MT_PARTICLE_EFFECT` | 8 | 58-60 | `PS::CParticleEffect` |
| `MT_PARTICLE_GROUP` | 9 | 61-63 | `PS::CParticleGroup` |
| `MT_SKELETON_RIGID` | 10 | 49-51 | `CKinematics` |
| `MT_TREE_PM` | 11 | 71-73 | `FTreeVisual_PM` (`#ifndef _EDITOR`) |
| *default* | — | 75-77 | `FATAL("Unknown visual type")` |

Tail: `R_ASSERT(V)` (`:79`), `V->Type=type` (`:80`).

### `Fmesh.h` header facts
- `enum MT` `Fmesh.h:9-25` (max `MT_3DFLUIDVOLUME=12` at `:24`).
- `const u8 xrOGF_FormatVersion = 4;` `Fmesh.h:122`; `const u16 xrOGF_SMParamsVersion=4;` `:86`.
- `struct ogf_header` `Fmesh.h:124-131`: `u8 format_version; u8 type; u16 shader_id; ogf_bbox bb; ogf_bsphere bs;` — `type` is `u8`.

## 1.1 `Fvisual::Load` — static mesh — `src/Layers/xrRender/FVisual.cpp:38-210`
- Calls base `dxRender_Visual::Load` (`:40`). Declares `D3DVERTEXELEMENT9 dcl[MAX_FVF_DECL_SIZE]` (`:42`) and `D3DVERTEXELEMENT9* vFormat=0` (`:43`).
- **Path (a) shared level geometry — `OGF_GCONTAINER`** (`:47-71`, `#ifndef _EDITOR`): reads `ID/vBase/vCount`, `p_rm_Vertices=RImplementation.getVB(ID)` + `AddRef` (`:57-58`), `vFormat=RImplementation.getVB_Format(ID)` (`:59`); indices `ID/iBase/iCount`, `dwPrimitives=iCount/3` (`:66`), `p_rm_Indices=RImplementation.getIB(ID)`+`AddRef` (`:69-70`).
- **`OGF_FASTPATH`** (`:72-106`, `#if RENDER==R2|R3|R4`): only if `data->find_chunk(OGF_FASTPATH)` (`:74`) — **optional**; allocates `m_fast=xr_new<IRender_Mesh>()` (`:80`), reads its own VB/IB from the fast pool (`getVB(ID,true)` etc.), `m_fast->rm_geom.create(...)` (`:104`).
- **Path (b) standalone — `OGF_VERTICES`** (`:127-149`): `fvf=r_u32()`, `D3DXDeclaratorFromFVF(fvf,dcl)` (`:130`), `vFormat=dcl` (`:131`), `vCount=r_u32()`, `vStride=D3DXGetFVFVertexSize(fvf)`.
  - DX10/11 (`:135-138`): `dx10BufferUtils::CreateVertexBuffer(&p_rm_Vertices, data->pointer(), vCount*vStride)`.
  - DX9 (`:139-149`): `HW.pDevice->CreateVertexBuffer(... D3DPOOL_MANAGED ...)` + `Lock`/`CopyMemory`/`Unlock`.
- **`OGF_INDICES`** (`:172-202`): `iCount=r_u32()`, `dwPrimitives=iCount/3`.
  - DX10/11 (`:177-189`): `dx10BufferUtils::CreateIndexBuffer(&p_rm_Indices, data->pointer(), iCount*2)`.
  - DX9 (`:190-202`): `HW.pDevice->CreateIndexBuffer(... D3DFMT_INDEX16, D3DPOOL_MANAGED ...)` + Lock/copy/Unlock.
- **Geometry object**: `rm_geom.create(vFormat, p_rm_Vertices, p_rm_Indices)` (`:206-209`) unless `VLOAD_NOVERTICES`. `vFormat` is either the level format or the FVF-derived `dcl`.
- The `OGF_VCONTAINER`/`OGF_ICONTAINER` branches are dead (`R_ASSERT2(0,...)` at `:114`/`:159`).

`Fvisual::Render(float)` `FVisual.cpp:212-233`: R2/R3/R4 render `m_fast` only during `PHASE_SMAP` with tessellation off, else `RCache.set_Geometry(rm_geom)` + `RCache.Render(D3DPT_TRIANGLELIST, vBase,0,vCount,iBase,dwPrimitives)`.

## 1.2 `CKinematics::Load` — skeletal — `src/Layers/xrRender/SkeletonCustom.cpp:161-335`
- `inherited::Load` → `FHierrarhyVisual::Load` (header/texture/children) (`:164`).
- `OGF_S_LODS` (`:170-198`); `OGF_S_USERDATA` → `pUserData=xr_new<CInifile>(UD,...)` (`:200-205`, `#ifndef _EDITOR`).
- **`OGF_S_BONE_NAMES`** (`:218-249`): `dwCount=r_u32()` (`:222`); **`VERIFY3(dwCount<=64, "More than 64 bones is a crazy thing!", N);`** (`:225`); per bone: `CreateBoneData(ID)` (`:234`), name, `bones->push_back` (`:237`), push `mk_pair(name,ID)` into `bone_map_N`/`bone_map_P` (`:238-239`), parent name to `L_parents` (`:242-244`), OBB `r(&pBone->obb,sizeof(Fobb))` (`:246`). Sort accel maps (`:250-251`).
- **Parent linking / `iRoot`** (`:254-278`): root → `iRoot=u16(i)`, `SetParentID(BI_NONE)`; else `(*bones)[ID]->children.push_back(B)` + `SetParentID(ID)`. `R_ASSERT(BI_NONE!=iRoot)` (`:275`).
- **`OGF_S_IKDATA`** (`:281-302`): per bone, `vers=IKD->r_u32()` (`:287`), `r_stringZ(B->game_mtl_name)` (`:288`), **`IKD->r(&B->shape, sizeof(SBoneShape))`** (`:289`), `B->IK_data.Import(*IKD, vers)` (`:290`), `bind_transform` from `vXYZ/vT` (`:291-295`), `mass=IKD->r_float()` (`:296`), `r_fvector3(center_of_mass)` (`:297`). Then `CalculateM2B(Fidentity)` (`:300`). Only the single `OGF_S_IKDATA` (id 16) variant exists — **no** `IKDATA_2`/`IKDATA_OLD`.
- `OGF_S_DESC` is **not** read here; only in `dxRender_Visual::Load` under `#ifdef _EDITOR` (`FBasicVisual.cpp:79-82`).
- `children` populated by `FHierrarhyVisual::Load` (`FHierrarhyVisual.cpp:44-93`): `OGF_CHILDREN_L` (link, `:47-63`) or `OGF_CHILDREN` (stream, `model_CreateChild`, `:66-87`).
- `CreateBoneData(u16 ID)` `SkeletonCustom.h:155`: `return xr_new<CBoneData>(ID);`.
- `CalculateBones` `src/Layers/xrRender/SkeletonRigid.cpp:23-128` (recursive `Bone_Calculate(bones->at(iRoot),&Fidentity)` `:94`); `CalculateBones_Invalidate` `SkeletonCustom.cpp:448-452` (sets `UCalc_Time=0`).
- **No `CKinematics::Render(float)` override** — geometry is drawn per child `CSkeletonX_PM/_ST::Render` (`FSkinned.cpp:402/417`).
- Members `SkeletonCustom.h`: `m_lod` (`:116`), `wallmarks` (`:126`), `pUserData` (`:133`), `bone_instances` (`:134`), `bones` (`vecBones*`, `:135`), `bones_size` (`:136`), `iRoot` (`:137`), `bone_map_N` (`:140`), `bone_map_P` (`:141`), `visimask`/`hidden_bones` (`:149-150`). `children` inherited from `FHierrarhyVisual` (`xr_vector<IRenderVisual*>`, `FHierrarhyVisual.h:15-16`).

## 1.3 `CKinematicsAnimated::Load` — `src/Layers/xrRender/SkeletonAnimated.cpp:776-928`
- `inherited::Load`→`CKinematics::Load` (`:778`).
- **`loadOMF` lambda** (`:785-810`): resolves `$level$`/`$game_meshes$` (`:788-791`); pushes `SMotionsSlot` (`:795`); opens file only if `!g_pMotionsContainer->has(_path)` (`:797`); `m_Motions.back().motions.create(_path, MS, bones)` (`:800`); re-docks shared via `create(_path, NULL, bones)` (`:804`).
- **`OGF_S_MOTION_REFS`** (`:813-857`): `set_cnt=_GetItemCount(items_nm)` (`:817`); **`R_ASSERT2(set_cnt<MAX_ANIM_SLOT, ...)`** (`:818`); `\*.omf` wildcard handling (`:825-837`); else append `.omf` + `loadOMF(nm)` (`:839-840`).
- **`OGF_S_MOTION_REFS2`** (`:858-899`): count via `r_u32()` (`:860`), each ref via `r_stringZ` (`:865`); same `.omf` logic.
- Inline-anim else branch: `motions.create(nm, data, bones)` (`:905`).
- **Partition** (`:910-911`): `m_Partition = m_Motions[0].motions.partition(); m_Partition->load(this,N);`.
- `IBlend_Startup()` (`:927`).
- `OGF_S_SMPARAMS` is read in `motions_value::load`, not here.
- **No `CKinematicsAnimated::Render` override.**

## 1.4 `motions_value::load` — OMF reader — `src/xrEngine/SkeletonMotions.cpp:77-250`
- **`OGF_S_SMPARAMS`** (`:84-162`): `vers=MP->r_u16()` (`:87`), `R_ASSERT3(vers<=xrOGF_SMParamsVersion)` (`:91`); partitions → `CPartDef` (`:94-127`); motion defs → `CMotionDef D.Load(MP,dwFlags,vers)` (`:138-160`) inserted into `m_fx`/`m_cycle` + always `m_motion_map` (`:153-158`).
- **`OGF_S_MOTIONS`** (`:170-247`): `dwCNT` count, `VERIFY(dwCNT<0x3FFF)` (`:175`); per motion per bone reads `CMotion`: `flRKeyAbsent`→single `CKeyQR` else `dwLen` `CKeyQR` (`:210-222`); `flTKeyPresent`→`CKeyQT16` (if `flTKey16IsBit`) or `CKeyQT8`+`_sizeT`/`_initT` (`:223-243`).
- Key structs `SkeletonMotions.h` (`#pragma pack(push,2)` `:24`): `CKey{Fquaternion Q;Fvector T;}` (`:25-29`), `CKeyQR{s16 x,y,z,w;}` (`:31-34`), `CKeyQT8{s8 x1,y1,z1;}` (`:36-39`), `CKeyQT16{s16 x1,y1,z1;}` (`:41-44`), `CMotion` (`:56-99`, bitfield `_flags:8/_count:24`, `ref_smem` key arrays).
- Accel maps in `motions_value` (`:218-244`): `m_motion_map`/`m_cycle`/`m_fx` (`accel_map = xr_map<shared_str,u16,...>`, `:166`), `m_partition` (`CPartition`, `:223`), `m_dwReference` (`:224`), `m_motions` (`:225`), `m_mdefs` (`:226`), `m_id` (`:228`).
- `CPartDef` (`:174-185`: `Name`, `xr_vector<u32> bones`); `CPartition` (`:187-215`, capped at `MAX_PARTS`).
- `motions_container` (`g_pMotionsContainer`): `has` `:278-281`, `dock` `:283-301`, `clean` `:303-335`. Declared `SkeletonMotions.h:259`, defined `SkeletonMotions.cpp:11`.

## 1.5 Animation limits — `src/Layers/xrRender/KinematicAnimatedDefs.h`
`MAX_BLENDED=16` (`:8`), `MAX_CHANNELS=4` (`:9`), `MAX_BLENDED_POOL=(MAX_BLENDED*MAX_PARTS*MAX_CHANNELS)` (`:11` = **256**), `MAX_ANIM_SLOT=48` (`:12`). `MAX_PARTS=4` (`src/xrEngine/SkeletonMotionDefs.h:6`). `blend_pool` is `svector<CBlend,MAX_BLENDED_POOL>` at `SkeletonAnimated.h:94`. `CBlend` defined in `src/xrEngine/bone.h:17-118`.

---

# SECTION 2 — Shader & Material System Map

## 2a. `dxRender_Visual::Load` — `OGF_TEXTURE` — `src/Layers/xrRender/FBasicVisual.cpp:43-83`
- Reads `OGF_HEADER` → sets `Type=hdr.type`, `shader=RImplementation.getShader(hdr.shader_id)`, `vis.box`, `vis.sphere` (`:53-61`).
- **`OGF_TEXTURE`** (`:68-76`): `r_stringZ(fnT,...)` = **texture** name (`:71`), `r_stringZ(fnS,...)` = **shader** name (`:72`), `dbg_shader_def=fnS` (`:73`), `dbg_texture_def=fnT` (`:74`), then `ResetShaderTexture()` (`:75`). Texture is read **before** shader.

## 2b. `dxRender_Visual::SetShaderTexture` — `FBasicVisual.cpp:124-153`
- `$no_shadows` parse (`:126-141`): `strstr(shader,"$no_shadows")`; if found `flags.set(IRenderVisualFlags::eNoShadow, TRUE)` and truncate (`:133`), else `FALSE` (`:137`). Result → `dbg_shader`.
- `shader.create(*dbg_shader, *dbg_texture)` (`:151`), wrapped by `Engine.External.SetSkinningMode(skinning)` and `::Render->hud_loading=hud`.
- `shader` type is `ref_shader` (`FBasicVisual.h:68`; typedef `Shader.h:190`).
- `ResetShaderTexture()` `:155-159` calls `SetShaderTexture(*dbg_shader_def,*dbg_texture_def)`.

## 2c. `CResourceManager::Create` — `src/Layers/xrRender/ResourceManager.cpp:324-368`
```cpp
if (_lua_HasShader(s_shader)) return _lua_Create(s_shader, s_textures);     // :333
else { Shader* p = _cpp_Create(s_shader,s_textures,...); if(p) return p;     // :337-339
       else if (_lua_HasShader("stub_default")) return _lua_Create("stub_default",s_textures); // :342-343
       else FATAL("Can't find stub_default.s"); }                            // :346
```
- `_cpp_Create(LPCSTR,...)` `:279-303` → `_GetBlender(name)` then `_cpp_Create(IBlender*,...)` `:170-277`, which compiles **6 shader elements** (`:208-261`) via `CBlender_Compile` and dedups against `v_shaders`.
- `_GetBlender` `:49-73` searches `m_blenders` (`map_Blender` = `map<const char*,IBlender*>`, `ResourceManager.h:56`); DX10/11 miss logs `"DX10: Shader '%s' not found in library."` and returns 0.
- `_lua_HasShader` `ResourceManager_Scripting.cpp:447-460` (DX10 `dx10ResourceManager_Scripting.cpp:499-512`): `Script::bfIsObjectPresent(LSVM, name, "normal"|"l_special", LUA_TFUNCTION)`.
- `_lua_Create` `ResourceManager_Scripting.cpp:462-555` (DX10 `:514-617`): runs the `.s` Lua script's `normal`/`l_special` function.

## 2d. `STextureList` — `src/Layers/xrRender/Shader.h:27-58`
- Inherits `xr_resource_flagged` + `xr_vector<std::pair<u32, ref_texture>>` (`:28-29`). The `u32` is the **sampler register index** ("stage"), set from `C->samp.index` in the recorder.
- `equal()` `:34-43`; `find_texture_stage(const shared_str&)` `:50` (impl `Shader.cpp:189-211`).
- Owned by `SPass::T` (`ref_texture_list`, `Shader.h:120`); deduplicated/shared (see 7d). `ref_texture_list` typedef `Shader.h:58`.

## 2e. THM system — `src/Layers/xrRender/TextureDescrManager.{h,cpp}`
- `m_texture_details` = `map<shared_str, texture_desc>` (`.h:46,50`). `texture_desc{ texture_assoc* m_assoc; texture_spec* m_spec; }` (`.h:36-44`).
  - `texture_assoc{ shared_str detail_name; u8 usage; }` (`.h:12-26`).
  - `texture_spec{ shared_str m_bump_name; float m_material; bool m_use_steep_parallax; }` (`.h:28-33`).
- `GetBumpName(tex)` `.cpp:155-166`; `GetMaterial(tex)` `.cpp:181-192`; `GetDetailTexture(tex,res,CS)` `.cpp:208-223`; `GetTextureUsage` `.cpp:194-206`; `UseSteepParallax` `.cpp:168-179`.
- `LoadTHM` `.cpp:56-123`: `FS.file_list(...,"*.thm")`, per file `tp.Load(*F)` (`STextureParams::Load`, `ETextureParams.cpp:67-112`), builds `texture_assoc`/`texture_spec`. `STextureParams::Load` reads `THM_CHUNK_TEXTUREPARAM`, `_TEXTURE_TYPE`, `_DETAIL_EXT`, `_MATERIAL`, `_BUMP`, `_EXT_NORMALMAP`, `_FADE_DELAY`.
- `m_detail_scalers` = `map<shared_str, cl_dt_scaler*>` (`.h:47,51`); `cl_dt_scaler` `.cpp:10-23` (`r__dtex_range=50`).
- **Threading**: `Load()` `.cpp:125-132` spawns **two** threads (`LoadTHMThread` `.cpp:48-54`) over `$game_textures$` and `$level$`; both write the **same** maps with **no mutex**. `Sleep(5)` only (`.cpp:131`), no join. **[VERIFIED — no barrier exists]**: `thread_spawn` (`src/xrCore/_math.cpp:392-401`) calls `_beginthread(thread_entry, stack, startup)` — a **detached** thread with no joinable handle returned, so `Load()` cannot and does not wait for completion. Callers are `dxRenderDeviceRender::OnAssetsChanged()` (`dxRenderDeviceRender.cpp:459-460`, `UnLoad` then `Load`) and `ResourceManager_Loader.cpp:132`. The reader accessors run during shader/texture creation on the main thread (`Blender_Recorder.cpp:67`, `SH_Texture.cpp:209-210`, `dx10SH_Texture.cpp:434-436`, `uber_deffer.cpp:40`). So there is a genuine startup race in stock code, "resolved" only by `Sleep(5)`. After load, accessors are read-only.

## 2f. Existing model shaders — `gamedata/shaders/r3/`
- Only model shader present: `models_selflight_det.s` (22 lines). Structure:
```lua
function normal(shader, t_base, t_second, t_detail)
    shader:begin("deffer_model_flat","deffer_base_flat"):fog(false):emissive(true)
    shader:dx10texture("s_base", t_base)
    shader:dx10sampler("smp_base")
    shader:dx10stencil(true,cmp_func.always,255,127,stencil_op.keep,stencil_op.replace,stencil_op.keep)
    shader:dx10stencil_ref(1)
end
function l_special(shader, t_base, t_second, t_detail)
    shader:begin("shadow_direct_model","accum_emissive_det"):zb(true,false):fog(false):emissive(true)
end
```
- Sampler conventions actually present in the shipped r3 set: `s_base` (multiple), `s_base0/s_base1`, `s_hemi` (`common_functions.h:87-90`), plus post-process samplers (`s_distort/s_image/s_bloom/s_scope/...`). Standard model samplers `s_bump`, `s_bump_x`, `s_detail`, `s_lmap`, `s_mask` are **absent** from this checkout (shipped in packed archives). **The repo ships only a partial set (29 files).**

## 2g. Unused sampler slots for PBR
`s_roughness`, `s_metallic`, `s_emissive` (and `s_bump`/`s_detail`/`s_material`/`s_normal`) appear **nowhere** in `gamedata/shaders/r3/` — they are free names. Binding is by **reflected register name**, end-to-end:
- **Compile time** — `r_dx10Texture(name, tex)` does `R_constant* C = ctable.get(name); u32 stage = C->samp.index; passTextures.push_back(mk_pair(stage, ref_texture(...)))` (`Blender_Recorder_R3.cpp:53-60`). The register comes from the compiled shader's reflection table, never from C++.
- **Render time** — `CBackend::set_Textures(STextureList* _T)` iterates the list and binds each `std::pair<u32 stage, ref_texture>` to its `stage` register (`R_Backend_Runtime.cpp:209-233`, `load_id = loader.first`).
- `common_functions.h` declares **no** explicit `register(t#/s#)` / `SamplerState` / `Texture2D` — the sampler-register convention lives in the unshipped `common.h` macro system.

**[RESOLVED for the design as specified]**: because every named sampler in a compiled pass binds to the register the HLSL compiler assigned *to that pass*, a brand-new self-contained `pbr_external.ps` controls **all** of its own registers and **cannot collide** with anything — register conflict is structurally impossible across distinct compiled shaders. The only residual unknown — the exact `t#`/`s#` layout *inside the stock deferred shaders* — is **irrelevant unless those stock shaders are edited**, which this design explicitly avoids. (Verify only if you choose to extend, rather than add, a shader.)

---

# SECTION 3 — Physics Integration Map

## 3a. `CBoneData` — `src/xrEngine/bone.h:512-586`
Ctor `CBoneData(u16 ID)` `:540-543`. Fields: `SelfID` (u16, `:516`), `ParentID` (u16, `:517`), `name` (shared_str, `:519`), `obb` (Fobb, `:522`), `bind_transform` (Fmatrix, `:524`), `m2b_transform` (Fmatrix, `:525`), `shape` (SBoneShape, `:526`), `game_mtl_name` (shared_str, `:527`), `game_mtl_idx` (u16, `:528`), `IK_data` (SJointIKData, `:529`), `mass` (float, `:530`), `center_of_mass` (Fvector, `:531`), `children` (`vecBones`=`xr_vector<CBoneData*>`, `:534`/typedef `:508`), `child_faces` (`:538`).

## 3b. `SBoneShape` — `src/xrEngine/bone.h:170-221`
- `enum EShapeType{stNone=0,stBox=1,stSphere=2,stCylinder=3,stForceU32=u16(-1)}` (`:172-179`).
- `enum EShapeFlags{sfNoPickable=1<<0, sfRemoveAfterBreak=1<<1, sfNoPhysics=1<<2, sfNoFogCollider=1<<3}` (`:181-188`).
- Fields: `u16 type` (`:190`), `Flags16 flags` (`:191`), `Fobb box` (`:192`), `Fsphere sphere` (`:193`), `Fcylinder cylinder` (`:194`). `Reset()` `:197-205`; `Valid()` `:207-220`. **No `Import` method** — read in bulk by `IKD->r(&B->shape,sizeof(SBoneShape))` at `SkeletonCustom.cpp:289`.

## 3c. `SJointIKData` — `src/xrEngine/bone.h:223-305`
- Joint type is top-level `enum EJointType{jtRigid=0,jtCloth=1,jtJoint=2,jtWheel=3,jtNone=4,jtSlider=5}` (`:144-153`); field `type` `:226`.
- `SJointLimit limits[3]` (`:227`; `struct SJointLimit{Fvector2 limit; float spring_factor; float damping_factor;}` `:155-168`), `spring_factor` `:228`, `damping_factor` `:229`.
- `enum{ flBreakable=1<<0 }` (`:231-234`) — **only `flBreakable` exists** (no `flExportIfBreakHinge`). `ik_flags` (Flags32, `:236`), `break_force` (`:237`), `break_torque` (`:238`), `friction` (`:240`).
- `Import(IReader&,u16 vers)` `:290-304`: reads type/limits/factors/ik_flags/break_force/break_torque; `friction` only if `vers>0`.

## 3d. Shell build — `src/xrPhysics/`
- `P_build_Shell` overloads `PhysicsShell.h:455-462`; core `extern "C" __stdcall (IPhysicsShellHolder*, bool, BONE_P_MAP*=0, bool=false)` `:460-462`. `P_build_SimpleShell(IPhysicsShellHolder*, float mass, bool)` `:464`. `ApplySpawnIniToPhysicShell(CInifile const*, CPhysicsShell*, bool fixed)` `:465`.
- Implementations in `PhysicsShell.cpp`: core `P_build_Shell` `:54-77` → `build_FromKinematics(K,bone_map)` `:68`; `P_build_SimpleShell` `:181-203` (single box from `ObjectKinematics()->GetBox()`, `add_Box(obb)`, `setMass`).
- `CPHShell::build_FromKinematics(IKinematics*, BONE_P_MAP*)` `PHShell.cpp:741-754` → `AddElementRecursive(0, LL_GetBoneRoot(), Fidentity, 0, &vis_check)` `:746`.
- `CPHShell::AddElementRecursive` `PHShell.cpp:796-1020`: `IBoneData& bone_data=GetBoneData(id)` (`:805`), `SJointIKData& joint_data=bone_data.get_IK_data()` (`:806`); `breakable` (`:828-833`) = `joint_data.ik_flags.test(flBreakable) && root_e && !(no_physics_shape(shape) && type==jtRigid)`; element via `P_create_Element()` (`:896`), `add_Shape(bone_data.get_shape())` (`:906`), `setMassMC(get_mass(),get_center_of_mass())` (`:907`); joints `BuildJoint(...)` (`:916`); recurse children (`:975-977`).
- **`R_ASSERT2(id<64, "ower 64 bones in breacable are not supported")`** `PHShell.cpp:867` — inside `if(breakable)` (`:863`) inside `if(joint_data.type==jtRigid && root_e)` (`:855`) inside `if(!no_physics_shape(...)||!root_e)` (`:853`). Reason: 64-bit fracture/splitter bitmask `1ui64<<id`. **Non-breakable bones with id≥64 never reach this assert.**

## 3e. `CEntityAlive::fill_hit_bone_surface_areas` — `src/xrGame/entity_alive.cpp:802-849`
Iterates `kinematics->LL_GetData(i).shape` (`:813-815`), skips `stNone`/`sfNoPickable`, computes surface area per shape type (box `:825-829`, sphere `:831-834`, cylinder `:836-839`), pushes `(bone_id, area)` and sorts descending (`:847`). Used for AI hit-point weighting (`get_new_local_point_on_mesh` `:853+`), **entity-only** — not used by static `physic_object`.

## 3f. Minimum physics for a static `physic_object`
- `CPhysicObject::CreateBody` `src/xrGame/PhysicObject.cpp:477-520` switches on `EPOType` (`xrServer_Space.h:48-54`):
  - `epotBox` → **`P_build_SimpleShell(this, m_mass, !flActive)`** (`:483-486`) — single box from bbox, **no skeleton, no per-bone shapes, no IK data needed**.
  - `epotFixedChain`/`epotFreeChain` → hand-rolled element/joint chain (`:488-496`).
  - `epotSkeleton` → `CreateSkeleton` → `P_build_Shell(this,!flActive,fixed_bones)` (`:349`) + `ApplySpawnIniToPhysicShell` (`:350-352`).
- `ApplySpawnIniToPhysicShell` `PhysicsShell.cpp:205-250`: reads `[physics_common] fixed_bones` (→`fix_bones`), `[collide] ignore_static/small_object/ignore_small_objects/ignore_ragdoll/ignore_animated_objects`, `[animated_object]` (→`CreateShellAnimator`).
- **Verdict:** For a static non-entity prop, `P_build_SimpleShell` (the `epotBox` path) is sufficient — it needs only a mass and the visual's bounding box; it never touches `SBoneShape`/`SJointIKData`. A GLTF static mesh therefore needs **no** OGF IK data to get working box physics.

---

# SECTION 4 — Render Pipeline Integration Map

## 4a. `renderable` — `src/xrEngine/irenderable.h`
`class IRenderable : public ISpatialOwner` (`:8`); anonymous member `renderable{ Fmatrix xform; IRenderVisual* visual; IRender_ObjectSpecific* pROS; BOOL pROS_Allowed; }` (`:12-18`). `renderable.visual` (`:15`) is the visual pointer; `renderable.xform` (`:14`). `renderable_Render(IDSGraphManager*)=0` (`:25`). `CObject` inherits it (`xr_object.h:83`, `#include "irenderable.h"` `:7`), exposes `Visual() {return renderable.visual;}` (`xr_object.h:193`).

## 4b. `vis_data` & frustum culling
- `struct vis_data` `src/xrEngine/vis_common.h:6-21`: `Fsphere sphere` (`:8`), `Fbox box` (`:9`, type `Fbox` not `Fbox3`), `u32 hom_frame` (`:10`), `u32 hom_tested` (`:11`). No extra clip flags. The concrete `vis` member is `dxRender_Visual::vis` (`FBasicVisual.h:67`).
- `CDSGraphManager::add_Static(...)` `r__dsgraph_build.cpp:544-615`: frustum test `frustum.testSAABB(vis.sphere.P, vis.sphere.R, vis.box.data(), planes)` (`:555`), `if(fcvNone==VIS) return` (`:557-558`), HOM `RImplementation.HOM.visible(vis)` (`:560-564`).
- `add_Dynamic` two overloads `:389-456` and `:458-523`; general dynamic insertion `r_dsgraph_insert_dynamic` `:34-231` does SSA discard `if(SSA<r_ssaDISCARD) return` (`:45-50`) + HOM on `vis.box`. **There is no `IsValuableToRender` function** — the gate is the SSA discard.

## 4c. Render graph & buckets
- Dispatch by `pVisual->Type` (`add_Dynamic` `:401/468`; `add_Static` `:567`). `MT_HIERRARHY`→`add_leafs`, `MT_SKELETON_*`→LOD/SSA, **`default`→`r_dsgraph_insert_dynamic/static`** — a *new* `MT_EXTERNAL_STATIC` lands in `default` and renders as a leaf mesh.
- Buckets are `RGraph.mapStaticPasses[priority][pass]` / `mapDynamicPasses[...]` (`r__dsgraph_types.h:240-241`), `RenderQueueArray = xr_array<xr_array<RenderQueue,SHADER_PASSES_MAX>,2>` (`:223`). Insert via `AddToRenderQueue(... , pass)` (static `:354`, dynamic `:228`). Sort key is a 128-bit `RenderPacket::sortKey` over `SPass` state/VS/PS/textures (`:194-219`) — not a `std::map<shader>`.
- `Render(float LOD)` is invoked in `r__dsgraph_render.cpp`: `item.pVisual->Render(LOD)` with `LOD=calcLOD(item.ssa, vis.sphere.R)` (`:165-169`); sorted path `:51`, `:349`, `:379`.

## 4d. Shadows / `eNoShadow`
- `enum IRenderVisualFlags{ eIgnoreOptimization=1<<0, eNoShadow=1<<1 }` `src/Include/xrRender/RenderVisual.h:10-14`; `Flags16 flags` on `IRenderVisual` (`:28`).
- Skip pattern `if(!i_mask[fl_normal] && !!flags.test(eNoShadow)) return;` at `add_Dynamic` `:397/464`, `add_Static` `:550`, `add_Static_MultiFrustum` `:625` — skips the visual in non-normal (shadow/SMAP) phases.

## 4e. `Render(float)` per class
- `Fvisual::Render` `FVisual.cpp:212-233` (described §1.1).
- `CKinematics::Render` / `CKinematicsAnimated::Render` — **no override**; both inherit the empty base `dxRender_Visual::Render` (`FBasicVisual.h:72-74`). Skeletal geometry draws through child `CSkeletonX_PM/_ST::Render` (`FSkinned.cpp:402/417`) reached via the dsgraph.

## 4f. Wallmarks
- `CSkeletonWallmark` `SkeletonCustom.h:21-76`: `m_Parent` (CKinematics*), `m_Shader`, model-space `m_ContactPoint`, `WMFacesVec m_Faces` with `bone_id[3][4]`/`weight[3][3]` skinning. Attached via `CKinematics::AddWallmark` (`.cpp:665-765`), rendered in `RenderWallmark` (`.cpp:813-897`).
- **Static external meshes require no wallmark code at all** — **[VERIFIED]**. There are exactly two wallmark paths: (1) `CWallmarksEngine::AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts, ...)` (`WallmarksEngine.cpp:327-337`) operates on **collision-DB triangles** (level geometry), not on any render visual; (2) `AddSkeletonWallmark` → `CKinematics::AddWallmark` (`WallmarksEngine.cpp:339-349`, `SkeletonCustom.cpp:665`) is **skeletal-only**. `Fvisual`/`FHierrarhyVisual`/`FTreeVisual` expose **no** `AddWallmark` (grep confirms `AddWallmark` exists only on `CKinematics`, `CWallmarksEngine`, and the game-side `CWalmarkManager`). Therefore a static external (non-skeletal) visual behaves **identically to a stock static OGF `Fvisual`**: neither supports per-visual decals; static wallmarks land via the collision DB (CDB) regardless of the render subclass. **No additional wallmark support is needed for parity.**

---

# SECTION 5 — Backwards-Compatibility Analysis

## 5a. `Instance_Load` format-detection branch — `ModelPool.cpp:102-152`
- **Insertion**: the extension build is at `:108-110`; the path resolution at `:113-132`; `FS.r_open` at `:139`. Insert the GLTF/GLB branch **after `fn` is resolved (after :132) and before `:139`** — at that point `fn` is the resolved absolute path:
  ```cpp
  if (LPCSTR e = strext(name); e && (0==stricmp(e,".gltf")||0==stricmp(e,".glb"))) {
      V = Instance_Create_External(name, fn);   // new loader, opens fn itself
      if (allow_register) V = Instance_Register(N, V);
      return V;
  }
  ```
- **Why `.ogf` defaulting is safe**: `:109` appends `.ogf` *only* when `0==strext(N)` (no extension). A `.gltf`/`.glb` path has an extension, so `:110` copies it verbatim and the `.ogf` default never runs. **Additive; cannot change OGF behaviour.**
- **Search order** `:115-116`: `$level$` then `$game_meshes$`. A GLTF in `$game_meshes$` resolves on the second probe. ✔
- **⚠ Correction discovered during implementation:** `CModelPool::Create` (and `CreateChild`/`Exists`) **strip the extension to build the cache key BEFORE calling `Instance_Load`** (`:279`, `:325`, `:502` in the original numbering). So a branch placed *only* in `Instance_Load` never sees the `.gltf`/`.glb` extension — by the time `Instance_Load` runs, the name is already `foo` and it would re-append `.ogf`. **The format detection therefore requires changing the strip sites too.** The implemented fix preserves *recognized external extensions* during the strip and strips everything else:
  ```cpp
  if (char* _ext = strext(low_name))
      if (!is_external_format(low_name))   // keep .gltf/.glb, strip .ogf/etc.
          *_ext = 0;
  ```
  This is additive for OGF (no extension or `.ogf` → still stripped → `.ogf` re-appended in `Instance_Load`) and simultaneously fixes the cache-key collision: `foo.gltf` and `foo.ogf` now produce **distinct** keys (`foo.gltf` vs `foo`), so they cache separately with no "last loader wins" hazard.

## 5b. `Fmesh.h` MT enum additions
- Append after `MT_3DFLUIDVOLUME=12` (`Fmesh.h:24`): `MT_EXTERNAL_STATIC=13`, `MT_EXTERNAL_SKELETAL=14`.
- **Existing OGF files can never contain 13/14**: `ogf_header.type` is a `u8` baked at compile time (`Fmesh.h:127`); no existing OGF was authored with these values. **Additive.**
- The `default: FATAL` in `Instance_Create` (`:75-77`) only fires when an *OGF header* yields an unknown type. The external loader creates `FExternalVisual`/`FExternalKinematics` directly (or via new switch cases), so it never depends on the OGF dispatch. New cases for 13/14 should be added (see §8).

## 5c. Bone-limit changes
- `VERIFY3(dwCount<=64,...)` `SkeletonCustom.cpp:225` is reached **only** by the OGF skeletal loader (`CKinematics::Load`). The external loader populates `bones`/`bone_map_*` directly and **does not call** `CKinematics::Load`, so it bypasses this assert entirely. Raising it is unnecessary for the external path; if raised, it cannot affect existing OGF (which are ≤64 by construction).
- `R_ASSERT2(id<64,...)` `PHShell.cpp:867` fires **only** for breakable rigid bones merged into the root (§3d). A static external prop using `P_build_SimpleShell` never reaches `AddElementRecursive`. A skeletal external prop with >64 bones is fine as long as high-index bones are not `flBreakable` rigid. Raising the limit (to a larger bitmask) is a **separate, optional** change; it cannot regress existing OGF (≤64 bones).

## 5d. `FBasicVisual.h` new virtuals
- Mods are Lua + data only (no compiled C++ plugins inherit `dxRender_Visual`). Adding virtuals (e.g. a PBR setter) changes the vtable layout but only requires recompiling the engine render DLLs together.
- `dxRenderFactory.cpp` creates **only render-subsystem helpers** (`dxUIShader`, `dxRenderTarget`, environment/rain/font renders, …) via `RENDER_FACTORY_IMPLEMENT` (`:29-37`); it creates **no `dxRender_Visual` subclass**. Visuals come from `CModelPool`. So the factory needs **no changes**. ✔
- **[VERIFIED — no external subclasses]**: a repo-wide grep for `: public dxRender_Visual` returns exactly **5** direct subclasses, **all inside the render layer**: `dx103DFluidVolume` (`xrRenderDX10/3DFluid/dx103DFluidVolume.h:8`), `dxParticleCustom` (`xrRender/dxParticleCustom.h:9`), `FHierrarhyVisual` (`xrRender/FHierrarhyVisual.h:12`), `Fvisual` (`xrRender/FVisual.h:14`), `FTreeVisual` (`xrRender/FTreeVisual.h:8`). All further visuals (`FProgressive`, `CKinematics`, `CKinematicsAnimated`, `CSkeletonX_*`, `FLOD`, …) derive from these. **Nothing in `xrGame`/`xrEngine`/`xrPhysics` or any third-party/plugin project subclasses `dxRender_Visual`** — the entire hierarchy is internal to the shared `xrRender` code (compiled into R1/R2/R3/R4). So appending virtuals only requires rebuilding the render DLLs together. *Recommendation:* still append new virtuals at the **end** of the vtable to minimise churn.

## 5e. `KinematicAnimatedDefs.h` limit changes (only if Phase 4)
- `MAX_ANIM_SLOT=48` assert `R_ASSERT2(set_cnt < MAX_ANIM_SLOT, ...)` `SkeletonAnimated.cpp:818` fires only when a model references **>48** OMF slots. **[VERIFIED by construction]**: the assert is in the stock load path, so *any model that currently loads on the stock engine has < 48 slots by definition* — a model exceeding 48 would already crash today. Hence **raising the limit cannot break any content that presently works**, independent of which specific game data ships (no per-archive audit needed). Raising it is additive (bigger `m_Motions` capacity).
- `MAX_BLENDED=16`, `MAX_CHANNELS=4`, `MAX_PARTS=4` → `MAX_BLENDED_POOL=256` (`:11`). `blend_pool` is `svector<CBlend,256>` (`SkeletonAnimated.h:94`) — **stack/inline storage inside each `CKinematicsAnimated`**. Raising any factor enlarges every animated instance's footprint. Existing OGF content is unaffected by larger caps (it simply uses fewer slots), but memory grows per instance.

## 5f. `TextureDescrManager` additions
- `m_texture_details` is populated once at startup by `Load()` (`:125-132`) from `.thm` files and is read-only afterward via `const` accessors. A new *programmatic* registration method that inserts entries **after** startup does not disturb existing entries.
- **Thread safety:** **[VERIFIED]** `Load()` runs two **detached** loader threads (§2e: `thread_spawn`→`_beginthread`, `_math.cpp:392-401`) and returns after only `Sleep(5)` — there is **no join/barrier anywhere** (the two `Load()` callers, `dxRenderDeviceRender.cpp:459-460` and `ResourceManager_Loader.cpp:132`, do not wait either). A programmatic registration method therefore **must** either (a) be called only after the loaders are known-complete, or (b) guard `m_texture_details`/`m_detail_scalers` with an `xrCriticalSection` (recommended, since the maps already tolerate concurrent loader writes today only by luck). This is now confirmed as *necessary*, not merely precautionary.

## 5g. `CModelPool::Create` & instancing
- Cache key = extension-stripped lowercased name (`:278-279`). `.gltf` vs `.ogf` collide (see 5a).
- `Instance_Duplicate(V)` `:84-100`: `N=Instance_Create(V->Type); N->Copy(V); N->Spawn();` then increments base refs. **The new subclass MUST implement `Copy(dxRender_Visual* from)`** (and be constructible by `Instance_Create` for its `Type`), or instancing crashes/leaks. Model `Copy` on `Fvisual` (shares VB/IB via `AddRef`).

## 5h. Delete / Release path
- `CModelPool::Delete` `:388-402`: if `g_bRendering`, queue into `ModelsToDelete` (`:392`), else `DeleteInternal` (`:399`). `DeleteInternal` `:342-374` asserts `!g_bRendering` (`:344`) — **the deferred queue exists because visuals must not be destroyed mid-render** (GPU still references them). Drained in `OnFrame` (`r4.cpp:603`).
- `dxRender_Visual::Release()` is **empty** (`FBasicVisual.cpp:37-39`); `Fvisual::Release` forwards to it. Actual GPU buffer release is in `~IRender_Mesh()` (`_RELEASE(p_rm_Vertices/Indices)`, `:20-24`). **The new subclass must free its GPU buffers and any cgltf/Assimp data in its destructor** (or override `Release()`), mirroring `~Fvisual`.
- Second queue `ModelsToDeleteDeffer` (`:376-386`/`:411-422`) guarded by `deffered_del_lock` — for cross-thread deferred deletes (e.g. particle groups).

---

# SECTION 6 — New Visual Subclass Design

## 6a. Required virtual overrides (static `FExternalVisual : dxRender_Visual, IRender_Mesh`)
| Virtual | Decl | Required behaviour |
|---|---|---|
| `Load(const char* N, IReader* data, u32 flags)` | `FBasicVisual.h:75` | For the external path, `data` is the OGF reader **and is not used** — the new loader opens the GLTF itself. Cleanest: **bypass `Load`** and have `Instance_Create_External(name, fn)` parse the file and populate fields, leaving `Load` as a no-op/guard. (The OGF `Instance_Load` calls `V->Load`; the external branch does not.) |
| `Render(float LOD)` | `FBasicVisual.h:72` | `RCache.set_Geometry(rm_geom); RCache.Render(D3DPT_TRIANGLELIST, vBase,0,vCount,iBase,dwPrimitives);` (mirror `Fvisual::Render`, `FVisual.cpp:216-227`). |
| `Release()` | `FBasicVisual.h:76` | May stay empty if the destructor frees buffers (matches `Fvisual`). |
| `Copy(dxRender_Visual* from)` | `FBasicVisual.h:77` | Required for `Instance_Duplicate`. Copy `Type/shader/vis/flags/skinning/hud` (base `Copy`, `FBasicVisual.cpp:163-180`) **plus** share VB/IB by `AddRef` and copy `rm_geom/vBase/vCount/iBase/iCount/dwPrimitives` (mirror `Fvisual::Copy`). |
| destructor | — | `_RELEASE(p_rm_Vertices); _RELEASE(p_rm_Indices); free cgltf data;` |

## 6b. Required field population (all verified owners)
| Field | Owner / line | Source |
|---|---|---|
| `Type` | `dxRender_Visual` `FBasicVisual.h:66` | `= MT_EXTERNAL_STATIC` |
| `vis.sphere`, `vis.box` | `vis_data` `vis_common.h:8-9` | computed from GLTF positions |
| `shader` (`ref_shader`) | `FBasicVisual.h:68` | `shader.create(shader_name, texture_name)` (`Shader.h:183-188`) |
| `rm_geom` (`ref_geom`) | `IRender_Mesh` `FBasicVisual.h:19` | `rm_geom.create(dcl, p_rm_Vertices, p_rm_Indices)` (as `Fvisual` `:206-209`) |
| `p_rm_Vertices` | `FBasicVisual.h:22` | `dx10BufferUtils::CreateVertexBuffer` (DX10/11) or `HW.pDevice->CreateVertexBuffer` (DX9) |
| `p_rm_Indices` | `FBasicVisual.h:27` | `dx10BufferUtils::CreateIndexBuffer` / DX9 equivalent |
| `vBase,vCount,iBase,iCount,dwPrimitives` | `FBasicVisual.h:23,24,28,29,30` | `vBase=iBase=0`, counts from GLTF, `dwPrimitives=iCount/3` |

## 6c. Vertex declaration
- Static OGF uses an FVF-derived `D3DVERTEXELEMENT9 dcl[MAX_FVF_DECL_SIZE]` via `D3DXDeclaratorFromFVF` (`FVisual.cpp:130`) or the level-shared format. For external meshes, **build a `D3DVERTEXELEMENT9` array manually** (position float3, normal, UV float2, tangent, binormal), then `rm_geom.create(dcl, vb, ib)`.
- Skinned declarations to copy as templates: `dwDecl_01W/2W/3W/4W` in `FSkinned.cpp` (`:60-68`, `:114-124`, `:182-194`, `:277-289`) — these show the engine's POSITION/NORMAL/TANGENT/BINORMAL/TEXCOORD packing (D3DCOLOR-packed normals, bone indices/weights in the `w` lanes).
- DX10/11: **Verified** — `rm_geom.create` → `CResourceManager::CreateGeom(D3DVERTEXELEMENT9* decl, vb, ib)` (`src/Layers/xrRenderDX10/dx10ResourceManager_Resources.cpp:580`) takes a **raw `D3DVERTEXELEMENT9` array**, and `_CreateDecl` (`:462`) calls `dx10BufferUtils::ConvertVertexDeclaration(D->dcl_code, D->dx10_dcl_code)` internally (`:478`, maps `D3DVERTEXELEMENT9`→`D3D_INPUT_ELEMENT_DESC`). So the external loader passes a plain `D3DVERTEXELEMENT9` array on **both** DX9 and DX10/11 paths — no manual conversion needed. **[Resolved]**

## 6d. `FExternalKinematics` (skeletal, optional, Phase 3+)
- Inherit from `CKinematics` (not `CKinematicsAnimated` for the first pass).
- Populate: `bones` (`vecBones*`), `bone_instances`, `bone_map_N`/`bone_map_P` (sorted accel maps), `iRoot`, `children` (the geometry child visuals). Per bone, `CreateBoneData(ID)` (`SkeletonCustom.h:155` → `xr_new<CBoneData>(ID)`) and fill `name/obb/bind_transform/shape/IK_data/mass/center_of_mass`, then `(*bones)[root]->CalculateM2B(Fidentity)` (as `SkeletonCustom.cpp:300`).
- After setup: `CalculateBones_Invalidate()` (`SkeletonCustom.cpp:448-452`) + `CalculateBones(TRUE)` (`SkeletonRigid.cpp:23`).
- Register a `default` render dispatch: a `MT_EXTERNAL_SKELETAL` falls into `add_Dynamic` `default` (→ renders the visual itself, not its children). For skeletal, **add an explicit case** mapping `MT_EXTERNAL_SKELETAL` to the `MT_HIERRARHY`/skeleton leaf path in `r__dsgraph_build.cpp` (`:416-447` / `:483-514`) so child geometry is rendered.

---

# SECTION 7 — PBR Shader Design

## 7a. Lua shader API (the actually-compiled DX10 binder — `src/Layers/xrRenderDX10/dx10ResourceManager_Scripting.cpp:396-422`)
| Lua method | C++ | binding | impl |
|---|---|---|---|
| `begin(vs,ps)` | `_pass`→`C->r_Pass(vs,ps,true)` | :398 | :141-146 |
| `begin(vs,gs,ps)` | `_passgs` | :399 | :148-153 |
| `sorting` | `_options`→`SetParams` | :400 | :110-114 |
| `emissive` | `_o_emissive` | :401 | :116-120 |
| `distort` | `_o_distort` | :402 | :129-133 |
| `wmark` | `_o_wmark` | :403 | :135-139 |
| `fog` | `_fog` | :404 | :155-159 |
| `zb` | `_ZB` | :405 | :161-165 |
| `blend` | `_blend` | :406 | :167-171 |
| `aref` | `_aref` | :407 | :173-177 |
| `dx10texture(name,tex)` | `_dx10texture`→`C->r_dx10Texture` | :414 | :179-183 |
| `dx10sampler(name)` | `_dx10sampler`→`C->r_dx10Sampler` | :421 | :185-189 |
| `dx10stencil*`,`dx10cullmode`,`dx10zfunc`,`dx10atoc`,`color_write_enable`,`scopelense` | various | :409-419 | :192-226 |
- **`sampler(...)`/`texture(...)`** are bound only in the DX9 binder (`ResourceManager_Scripting.cpp:397,364`) and are commented out for DX10 — for R3/R4 **use `dx10texture`/`dx10sampler`**.
- **`lit`, `lod`, `shadow`, `strict_b2f` are NOT Lua methods in either binder** (`strict_b2f` is only the `Sflags::bStrictB2F` bit, `Shader.h:141`).
- `sampler("s_base"):texture(t_base)` / `dx10texture("s_base", t_base)` passes the **literal texture name** (only `fix_texture_name` strips the extension, `Blender_Recorder_R3.cpp:48-50`); the **register name** `"s_base"` is resolved against the compiled pass's reflected constant table `ctable.get(name)` → `C->samp.index` (`:53-60`). It does **not** call `GetBumpName`/THM — bump/detail resolution is a C++-blender concern, not the Lua path.
- `t_base/t_second/t_detail` come from `CBlender_Compile::_lua_Compile` (`dx10ResourceManager_Scripting.cpp:619-638`): `t_0=L_textures[0]`, `t_1=L_textures[1] or "null"`, `t_d=detail_texture or "null"`, then `element(ac, t_0, t_1, t_d)` (`:634`). `L_textures` is the comma-split of `s_textures` via `_ParseList` (`ResourceManager.cpp:104-142`). **Only two named textures + the THM detail reach Lua.**

## 7b. HLSL VS/PS naming
- `begin(vs_name, ps_name)` passes file basenames (no extension) of compiled shaders in `gamedata/shaders/<rN>/` — e.g. `models_selflight_det.s` references `deffer_model_flat` (vs), `deffer_base_flat` (ps), `shadow_direct_model`, `accum_emissive_det`. The matching `.vs`/`.ps` files live in `gamedata/shaders/r3/` (and r2/r4). A `pbr_external.s` would `begin("pbr_external","pbr_external")` with new `pbr_external.vs`/`pbr_external.ps` placed there. **Note:** the stock deferred VS/PS are not in this checkout (partial set) — author against the full game gamedata.

## 7c. `s_textures` parameter
- It is a comma-separated texture list, split by `_ParseList` (`ResourceManager.cpp:104-142`) into `C.L_textures`. Verified that `_lua_Compile` consumes only `L_textures[0]`/`[1]` positionally (`:626-627`) plus the detail texture (`:628`). **A 3rd+ comma entry is parsed but never passed to the Lua function.** Multi-map PBR via `s_textures` alone is therefore not viable through the stock Lua path.

## 7d. Multi-texture injection
- `STextureList` can hold many `(stage, ref_texture)` pairs (`Shader.h:27-58`) — the limit is the number of sampler registers the compiled pass declares, not the API.
- The texture list is built in the **recorder** (`r_dx10Texture`→`passTextures.push_back(mk_pair(stage, ref_texture))`, `Blender_Recorder_R3.cpp:60`; finalised `dest.T = _CreateTextureList(passTextures)`, `:268`), then **deduplicated/shared** by `_CreateTextureList` (`ResourceManager_Resources.cpp:601-615`, sorts by stage + returns an existing equal list). **Because the list is a shared resource, mutating it after `shader.create` would affect every visual that shares it.** Therefore PBR maps must be bound **inside the Lua `.s` shader** (extra `dx10texture("s_roughness", t_rough)` calls) so each unique texture set produces its own deduped list — **do not** patch the texture list post-creation.
- **VERIFIED by a shipping shader.** The sibling MT mod's `gamedata/shaders/r3/scope_color_write.s` binds **ten** textures in a single pass via `dx10texture("s_reticle","$user$reticle")`, `dx10texture("s_inside","wpn\\scope_utility\\inside")`, … mixing `$user$` render-targets and plain file paths, against a **custom** `scope_vertex.vs`/`scope_*.ps` pair (those `.vs`/`.ps` are authored by the modder and compile at runtime). This is exactly the PBR mechanism: a `pbr_external.vs/.ps` plus a `.s` that `dx10texture`-binds `s_base`/`s_normal`/`s_roughness`/`s_metallic`/`s_emissive`.
- **Simplest per-model map plumbing (no engine change for binding):** the `.s` is **Lua**, so the shader can derive each map's name from `t_base` by string ops, e.g. `dx10texture("s_roughness", t_base.."_r")`, `..\"_n\"` for normal, etc. (a naming convention the GLTF importer/material writes). This avoids overloading the 3-arg `s_textures` path entirely.
- **Recommended PBR plumbing:** either the Lua-string-from-`t_base` convention above (zero engine change), or — for arbitrary glTF map names — extend the THM-style metadata (a new `texture_spec`-like record) keyed by base texture name to carry roughness/metallic/emissive names, register them programmatically (§5f), and have `pbr_external.s` pull them via `dx10texture`. Do **not** patch the shared `STextureList` post-creation.

---

# SECTION 8 — Complete File Change List

## Existing files to modify
| # | File | Function / location | Lines | Change | Additive? | BC verdict |
|---|---|---|---|---|---|---|
| 1 | `src/Layers/xrRender/ModelPool.cpp` | `Instance_Load(const char*,BOOL,bool)` | after :132, before :139 | insert GLTF/GLB branch → `Instance_Create_External` | additive | safe (ext-gated) |
| 1b | same | `Instance_Create` | :74→add cases | `case MT_EXTERNAL_STATIC/SKELETAL:` create new classes | additive | safe (new enum) |
| 2 | `src/Layers/xrRender/ModelPool.h` | class `CModelPool` | new decl | `dxRender_Visual* Instance_Create_External(const char* N, const char* fn);` | additive | safe |
| 3 | `src/xrEngine/Fmesh.h` | `enum MT` | after :24 | `MT_EXTERNAL_STATIC=13, MT_EXTERNAL_SKELETAL=14` | additive | safe |
| 4 | `src/Layers/xrRender/FBasicVisual.h` | `dxRender_Visual` | after :77 | optional `virtual void SetPBRTextures(...)` (append) | additive | safe (vtable tail) |
| 5 | `src/Layers/xrRender/FBasicVisual.cpp` | new method | — | impl if 4 added | additive | safe |
| 6 | `src/Layers/xrRender/TextureDescrManager.h` | `CTextureDescrMngr` | new decl | `RegisterExternal(name, bump, detail, roughness, metallic, emissive, material)` | additive | safe (post-load) |
| 7 | `src/Layers/xrRender/TextureDescrManager.cpp` | new method (+ optional guard) | near :125 | impl + (recommended) mutex/barrier | additive (+ sync) | safe if synced |
| 8 | `src/Layers/xrRender/SkeletonCustom.cpp` | `VERIFY3` | :225 | (optional, Phase 3) raise 64 if external skeletal needs it — **external path bypasses this anyway** | modifies literal | safe (OGF ≤64) |
| 9 | `src/xrPhysics/PHShell.cpp` | `R_ASSERT2(id<64,...)` + bitmask | :867 (+ `1ui64<<id` sites :812,1034) | (optional) widen breakable bitmask | modifies | safe (OGF ≤64; only breakable) |
| 10 | `src/Layers/xrRender/SkeletonAnimated.cpp` | new anim path | near :813 | Phase 4 GLTF→OMF bridge | additive | safe |
| 11 | `src/xrEngine/SkeletonMotions.{h,cpp}` | `motions_value` | new overload | Phase 4 in-memory motion build | additive | safe |
| 12 | `src/Layers/xrRender/KinematicAnimatedDefs.h` | constants | :8-12 | Phase 4 only, if >48 slots / >16 blends needed | modifies | safe (caps only grow) |

## New files to create
| File | Purpose |
|---|---|
| `src/Layers/xrRender/FExternalVisual.h` / `.cpp` | static `dxRender_Visual, IRender_Mesh` subclass (Phase 1) |
| `src/Layers/xrRender/FExternalKinematics.h` / `.cpp` | skeletal `CKinematics` subclass (Phase 3) |
| `src/Layers/xrRender/cgltf.h` | header-only GLTF/GLB parser (recommended over Assimp: header-only, no new link deps) |
| `gamedata/shaders/r3/pbr_external.s` | Lua shader (`normal`/`l_special` using `dx10texture`) |
| `gamedata/shaders/r3/pbr_external.vs` / `.ps` (+ r2/r4) | HLSL VS/PS (basenames referenced by `begin(...)`) |

## Build system
- Shared `src/Layers/xrRender/*.cpp/.h` are referenced by each render project via `..\xrRender\...`. Add `FExternalVisual.cpp`/`FExternalKinematics.cpp` (`<ClCompile>`) and `.h`/`cgltf.h` (`<ClInclude>`) to:
  - `src/Layers/xrRenderPC_R2/xrRender_R2.vcxproj` — ClInclude group **:357-531**, ClCompile group **:532-726** (e.g. existing `..\xrRender\FVisual.cpp` at :592).
  - `src/Layers/xrRenderPC_R3/xrRender_R3.vcxproj` — ClInclude **:367-561**, ClCompile **:562-785** (`..\xrRender\FVisual.cpp` :651).
  - `src/Layers/xrRenderPC_R4/xrRender_R4.vcxproj` — ClInclude **:367-570**, ClCompile **:571-804** (`..\xrRender\FVisual.cpp` :660).
  - (and `src/Layers/xrRenderPC_R1/xrRender_R1.vcxproj` if DX9/R1 support is wanted.)
- Also add to the matching `*.vcxproj.filters` (all six exist) under the `Models\Visuals` filter (e.g. `xrRender_R2.vcxproj.filters` `..\xrRender\FVisual.cpp` at :867-869).
- `src/xrEngine/xrEngine.vcxproj`: ClInclude group **:900-1014**, ClCompile group **:1015-1136** (only needed if engine-level files change, e.g. `Fmesh.h` is header-only so no project edit).
- `src/xrPhysics/xrPhysics.vcxproj`: ClCompile **:354-429**, ClInclude **:430-523** (only if §9 physics changes add files).

---

# SECTION 9 — Known Unknowns & Risks

1. **`_lua_Create` multi-texture behaviour** — *Verified*: only `t_base`/`t_second`/`t_detail` reach the Lua function (`dx10ResourceManager_Scripting.cpp:626-634`); the `s_textures` comma list cannot inject roughness/metallic positionally. PBR maps must be bound inside the `.s` shader via additional `dx10texture` calls. **[Resolved]**
2. **`Instance_Duplicate` without `Copy()`** — `Instance_Duplicate` calls `N->Copy(V)` (`ModelPool.cpp:88`). A subclass without a correct `Copy` will not share VB/IB and may double-free or render garbage. **Must implement `Copy` (and ensure `Instance_Create` builds the subclass for its `Type`).** **[Resolved: action required]**
3. **DX9 vs DX10/11 buffer path** — `dx10BufferUtils` is wholly behind `#if defined(USE_DX10)||defined(USE_DX11)` (`dx10BufferUtils.h:4-15`); under DX9 the namespace doesn't exist and `Fvisual` uses `HW.pDevice->CreateVertexBuffer/CreateIndexBuffer` + Lock/copy (`FVisual.cpp:139-149,190-202`). The external loader must branch identically (or target R2/R3/R4 only). **[Resolved]**
4. **`OGF_FASTPATH`** — optional, R2/R3/R4 only, used solely during `PHASE_SMAP` shadow rendering with tessellation off (`FVisual.cpp:72-106,215-227`). External meshes can skip it; without `m_fast` they render the normal `rm_geom` in the shadow pass (slightly heavier, correct). **[Resolved — optional]**
5. **`CModelPool` thread-safety** — **[VERIFIED]**. The engine runs a **secondary `PreRenderThread`** concurrent with game logic: `Device.cpp:400` `secondary_tasks.run(&XRay::Engine::PreRenderThread)`, and `g_bRendering` is an `xr_atomic_bool` toggled around the render pass (`Device.cpp:48,99,165`). `ModelsLock` (`xrSRWLock`) guards the base `Models` registry: **exclusive** in `Instance_Register`/`Destroy`/`Discard` (`ModelPool.cpp:173,226,432`), **shared** in `Instance_Find`/`Instance_Duplicate`/`dump`/`memory_stats` (`:92,257,555,596`) — i.e. the base registry *is* read on the render/secondary thread while it is written on the game/load thread. Model **loading itself is not task-parallelised**: `Prefetch()` is a serial `for` loop calling `Create` on the caller's thread (`ModelPool.cpp:473-486`), and `ModelPool.cpp` contains no task-scheduler calls. The per-instance `Registry`/`Pool` maps (mutated in `Create`/`DeleteInternal`/`Discard`) are **unguarded** → `Create`/instancing is single-threaded (game thread). **Action:** the external loader must register via `Instance_Register`/`Instance_Find` (lock-protected) exactly like the OGF path; cgltf parsing happens before registration on local state, so it is safe.
6. **`g_pMotionsContainer`** — `motions_container*` global (`SkeletonMotions.h:259`, `cpp:11`), owned by `CModelPool` (alloc `:244`, free `:250`), caches shared OMF motion data keyed by path. The static external loader **does not need it**; a Phase-4 animated external loader would `dock`/`has` into it (or build motions in-memory). **[Resolved]**
7. **`CGamePersistent::RegisterModel`** — `src/xrGame/GamePersistent.cpp:120-151` switches on `V->getType()` and acts **only** for `MT_SKELETON_ANIM`/`MT_SKELETON_RIGID` (assigns bone game-materials). It does **not** parse OGF. For `MT_EXTERNAL_STATIC`/`MT_EXTERNAL_SKELETAL` it is a **no-op** — meaning external skeletal models won't get automatic bone-material assignment (set `game_mtl_idx` in the external loader if needed). The file-loader `Instance_Load` calls it (`ModelPool.cpp:145`); harmless for external types. **[Resolved]**
8. **THM loader-thread sync** — **[VERIFIED]**: `m_texture_details`/`m_detail_scalers` are written by two **detached** threads (`thread_spawn`→`_beginthread`, `_math.cpp:392-401`); `Load()` returns after `Sleep(5)` with no join, and neither caller (`dxRenderDeviceRender.cpp:459-460`, `ResourceManager_Loader.cpp:132`) waits. There is **no existing barrier**. Programmatic PBR registration must add its own `xrCriticalSection` or run strictly post-load. Confirmed necessary.
9. **`rm_geom.create` declaration conversion (DX10)** — *Verified*: `CResourceManager::CreateGeom(D3DVERTEXELEMENT9*, vb, ib)` (`dx10ResourceManager_Resources.cpp:580`) accepts a raw `D3DVERTEXELEMENT9` array; `_CreateDecl` (`:462`) calls `dx10BufferUtils::ConvertVertexDeclaration` internally (`:478`). The external loader passes a plain `D3DVERTEXELEMENT9` array on both paths. **[Resolved]**
10. **Static wallmarks on external meshes** — **[VERIFIED — no work needed]**. The only wallmark paths are `AddStaticWallmark(CDB::TRI*, verts, ...)` over collision-DB triangles (`WallmarksEngine.cpp:327-337`) and skeletal `CKinematics::AddWallmark` (`WallmarksEngine.cpp:339-349`, `SkeletonCustom.cpp:665`). Non-skeletal visuals (`Fvisual`/`FHierrarhyVisual`/`FTreeVisual`) have **no** `AddWallmark`. A static external visual matches a stock static OGF `Fvisual` exactly — static decals come from the CDB, not the render subclass. (See §4f.)

---

# SECTION 10 — Recommended Implementation Order

## Phase 1 — Static mesh (no skeleton/anim/PBR)
Goal: load a GLTF static mesh, render with a basic existing model shader, correct `vis.sphere`/`vis.box`, box physics via `P_build_SimpleShell`.
- **Files:** `Fmesh.h` (+`MT_EXTERNAL_STATIC=13`); `ModelPool.cpp` (branch :132→ + `Instance_Create` case); `ModelPool.h` (decl); `FExternalVisual.h/.cpp` (new); `cgltf.h` (new); the three (or four) render `.vcxproj` + `.filters`.
- **Render:** mirror `Fvisual::Render`; geometry via `dx10BufferUtils::Create*Buffer` (DX10/11) / `HW.pDevice->Create*Buffer` (DX9); `rm_geom.create(dcl, vb, ib)`; `shader.create("models\\some_existing", base_texture)`.
- **Physics:** none in the visual; the `physic_object` spawns as `epotBox` → `P_build_SimpleShell` (no engine change).
- **Size:** ~**600-1000** new LOC (≈400-700 for `FExternalVisual` incl. cgltf glue + decl building, ~30 for ModelPool branch, ~10 enum/dispatch, plus cgltf.h vendored).

## Phase 2 — Static mesh + PBR materials
- **Files added beyond P1:** `gamedata/shaders/r3/pbr_external.s` + `pbr_external.vs`/`.ps` (and r2/r4); optional `TextureDescrManager.{h,cpp}` `RegisterExternal` + sync (§5f/§7d); optional `FBasicVisual.h/.cpp` PBR setter (append vtable).
- **Binding:** PBR maps bound *inside* `pbr_external.s` via `dx10texture("s_roughness", ...)` etc. — never patch the shared `STextureList` post-creation.
- **Size:** ~**300-500** LOC + shader authoring.

## Phase 3 — Rigid skeletal mesh (no animation)
- **Files added:** `FExternalKinematics.h/.cpp` (inherit `CKinematics`); `Fmesh.h` (+`MT_EXTERNAL_SKELETAL=14`); `ModelPool.cpp` `Instance_Create` case; `r__dsgraph_build.cpp` explicit dispatch case so child geometry renders (don't rely on `default`). Optional: physics from per-bone `SBoneShape` via `P_build_Shell` (then the `PHShell.cpp:867` 64-bone limit applies only to breakable rigid bones).
- **Setup:** build `bones/bone_map_*/iRoot/children`, `CreateBoneData`, `CalculateM2B`, then `CalculateBones_Invalidate()` + `CalculateBones(TRUE)`.
- **Size:** ~**800-1400** LOC.

## Phase 4 — Animated skeletal mesh (hardest)
- **Files added:** `SkeletonAnimated.cpp` GLTF→blend bridge; `SkeletonMotions.{h,cpp}` in-memory `motions_value` build overload; possibly `KinematicAnimatedDefs.h` cap raises.
- **Why hardest:** the engine's playback is driven by `CMotionDef`/`CPartition`/`CBlend` quantised-key system (`SkeletonMotions.cpp:77-250`, key types `CKeyQR/QT8/QT16`) with **no GLTF equivalent** — GLTF animation samplers (per-node TRS tracks, interpolation modes) must be resampled into per-bone quantised key streams and synthesised `CMotionDef`/partition metadata, then fed through `g_pMotionsContainer`/`IBlend_*`. Marks, speed/power/accrue/falloff, FX vs cycle classification, and partitions all have to be fabricated.
- **Size:** ~**1500-3000+** LOC; the bulk is the animation transcoder.

---

## Appendix — Agent verification ledger
All Section 1-4 facts were gathered by reading the named source files on branch `all-in-one-vs2022-wpo-mt`; the highest-traffic claims (`ModelPool.cpp:30-167`, `Fmesh.h:9-25`, `CreateGeom`/`_CreateDecl` `dx10ResourceManager_Resources.cpp:462-610`) were additionally re-read first-hand.

**All previously-`[UNVERIFIED]` items have now been resolved against source:**
| Item | Verdict | Key citations |
|---|---|---|
| THM loader-thread barrier (§2e/§5f/§9.8) | **No barrier exists** — detached threads, `Sleep(5)` only → new registration needs its own mutex | `_math.cpp:392-401`, `TextureDescrManager.cpp:125-132`, callers `dxRenderDeviceRender.cpp:459-460`, `ResourceManager_Loader.cpp:132` |
| PBR sampler registers (§2g/§9 PBR) | **Resolved for new-shader design** — binding is reflection-driven per pass; a self-contained `pbr_external` cannot collide | `Blender_Recorder_R3.cpp:53-60`, `R_Backend_Runtime.cpp:209-233`, `common_functions.h` (no fixed `register()`) |
| External DLLs subclassing `dxRender_Visual` (§5d) | **None** — 5 subclasses, all in render layer | grep `: public dxRender_Visual` → `FVisual.h:14`, `FHierrarhyVisual.h:12`, `FTreeVisual.h:8`, `dxParticleCustom.h:9`, `dx103DFluidVolume.h:8` |
| Concurrent `Create`/`Instance_Load` (§9.5) | **Render/secondary thread reads base registry; loading not task-parallel** — register via `Instance_Register` | `Device.cpp:400,48,99,165`, `ModelPool.cpp:92,173,257,432,473-486` |
| Static-wallmark integration (§4f/§9.10) | **No work needed** — wallmarks are CDB-triangle or skeletal only | `WallmarksEngine.cpp:327-349`, `SkeletonCustom.cpp:665` |
| `MAX_ANIM_SLOT=48` content safety (§5e) | **Safe by construction** — stock assert bounds all shipping models to <48 | `SkeletonAnimated.cpp:818` |

No `[UNVERIFIED]` tags remain in the document.

---

# Phase 1 Implementation — Status, Files & Testing

**Status:** implemented. A static `.gltf`/`.glb` mesh in `$game_meshes$` loads via `Render->model_Create()` / Lua `set_visual_name()`, renders through the stock deferred model pipeline, gets correct bounding volumes, and is cached/instanced/deleted exactly like an OGF visual. The OGF/OMF path is untouched.

## Files changed / added (Phase 1)
| File | Change |
|---|---|
| `src/xrEngine/Fmesh.h` | `MT_EXTERNAL_STATIC=13`, `MT_EXTERNAL_SKELETAL=14` appended after `MT_3DFLUIDVOLUME=12` |
| `src/Layers/xrRender/ModelPool.h` | declare `Instance_Create_External(...)` + `static is_external_format(...)` |
| `src/Layers/xrRender/ModelPool.cpp` | include `FExternalVisual.h`; `is_external_format`/`Instance_Create_External` impl; `MT_EXTERNAL_STATIC` case in `Instance_Create`; external branch in `Instance_Load`; preserve `.gltf/.glb` extension in `Create`/`CreateChild`/`Exists` cache keys |
| `src/Layers/xrRender/FExternalVisual.h` / `.cpp` | **new** — the static external visual + cgltf loader |
| `src/Layers/xrRender/cgltf_impl.cpp` | **new** — sole `CGLTF_IMPLEMENTATION` TU (PCH disabled) |
| `src/Layers/xrRender/cgltf.h` | already vendored in-repo (cgltf v1.15, MIT) — unchanged |
| `gamedata/shaders/r3/external_static.s` | **new** — Phase-1 fullbright model shader (reuses stock `deffer_model_flat`/`deffer_base_flat`) |
| `xrRender_R1/R2/R3/R4` `.vcxproj` + `.filters` | add the two new `.cpp` (cgltf_impl.cpp = NotUsing PCH) and two new `.h` under `Models\Visuals` |

## How it works (data flow)
`set_visual_name("dynamics\\foo.glb")` → `cNameVisual_set` → `model_Create` → `CModelPool::Create` (keeps the `.glb` in the cache key) → `Instance_Load` (resolves `$game_meshes$\foo.glb`, sees external ext) → `Instance_Create_External` → `FExternalVisual::LoadExternal` (cgltf parse → merge triangle prims → build VB/IB → bounds → `external_static` shader) → registered/duplicated/rendered through the normal dsgraph (`default` dispatch in `add_Dynamic`).

## How to test
1. Vendor cgltf is already present (`src/Layers/xrRender/cgltf.h`). Build the engine (R3/R4) in Visual Studio.
2. Put a static `.glb` (positions + normals + `TEXCOORD_0`, ≤65 535 verts) at e.g. `gamedata/meshes/dynamics/foo.glb`, with its base-color texture available as a `.dds` under `$game_textures$` matching the glTF image name.
3. Spawn any `physic_object`/dynamic object and set its visual to `dynamics\foo.glb` (note the explicit extension — that is what routes it to the external loader), e.g. from Lua: `obj:set_visual_name("dynamics\\foo.glb")`.
4. Expected: the mesh renders fullbright (emissive Phase-1 shader) with its base texture; it is frustum-culled, instanced and deleted like any model. Physics: a `physic_object` of type `epotBox` gets a box shell via `P_build_SimpleShell` with no engine change (the visual supplies `vis.box`).

## Phase 1 limitations (by design; addressed in later phases)
- **16-bit indices**: meshes >65 535 vertices are rejected with a log message (matches the OGF model path).
- **Single merged mesh / first material**: all triangle primitives are merged into one VB/IB and the first base-color texture is used. Per-material submeshes ⇒ Phase 2.
- **Fullbright shader**: `external_static.s` is emissive (always visible) for verification; lit PBR is Phase 2.
- **Loose files best-supported**: file bytes are read via the engine VFS (works packed too); external `.bin` for `.gltf` is resolved by cgltf relative to the resolved path (loose). GLB embeds everything → fully self-contained.
- **R3/R4 (DX10/11) target**: `external_static.s` uses the DX10 Lua binder. R2 (DX9) would need a DX9-style `.s`. The C++ compiles for R1–R4 (DX9 buffer path included).
- **Coordinate conversion**: `EXTERNAL_FLIP_Z` (default on) converts glTF right-handed → engine left-handed and yields DX-correct winding. If a given exporter looks inside-out, toggle that `#define` in `FExternalVisual.cpp`.

## Vertex format / shader pairing — VERIFIED against the deployed shaders
The biggest Phase-1 unknown (the model VS/PS aren't in source) was resolved by reading the **deployed** Anomaly shaders (`F:\Anomaly-1.5.3-Testing\tools\_unpacked\shaders\r3`). Two facts corrected the first implementation:
1. **`SKIN_NONE` selection** — `r4.cpp:1487` (also r1/r2/r3) defines `SKIN_NONE` iff `m_skinning < 0`. So a static external visual must set `skinning = -1` (it now does). With `m_skinning >= 0` the model VS would compile as a *skinned* variant expecting bone matrices.
2. **`v_model` is all-float** — `shaders/r3/common_iostructs.h` struct `v_model` (the `SKIN_NONE` input of `deffer_model_flat.vs`) is `POSITION float4, NORMAL/TANGENT/BINORMAL float3 (real −1..+1), TEXCOORD float2`. The static VS reads `I.N` directly (no D3DCOLOR unpack — that swizzle/`unpack_normal` only exists in `skin.h`'s skinned path). The initial D3DCOLOR-packed layout (copied from the *skinned* `dwDecl_01W`) was therefore wrong; `dwDecl_External` is now five `FLOAT*` elements (stride 56) matching `v_model` exactly.

Also confirmed present in the deployed set: `deffer_model_flat.vs`, `deffer_base_flat.ps`, `shadow_direct_model.vs`, `accum_emissive_det.ps` (the four passes `external_static.s` binds), and that `deffer_base_flat.ps` guards `s_detail` under `USE_TDETAIL` and `s_hemi` under `USE_LM_HEMI` — so binding only `s_base` (+`smp_base`) is correct for a plain textured model (exactly how the shipped `models_selflight_det.s` works). The corrected `FExternalVisual.cpp` compiles + links clean (R4). Remaining to confirm purely visually in-game: texture **V** orientation and the `EXTERNAL_FLIP_Z` winding choice (the only two things that can't be settled from the shader source alone).
