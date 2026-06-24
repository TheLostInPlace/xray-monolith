# glTF 2.0 Conformance Audit — X-Ray Monolith External Loader

**Date:** 2026-06-23. Method: 5 parallel deep-research passes (skinning/animation, materials/PBR,
geometry/accessors, textures/images, scene-graph/physics/extensions), each cross-referencing our
actual code against the Khronos glTF 2.0 spec + reference loaders (cgltf, three.js, Babylon).

Goal: **as close to 1:1 with glTF as the engine allows**, without breaking OGF/OMF/stock.

---

## Executive summary

The core is **sound**. The RH→LH coordinate conversion, the node-world bake, the skinning math
(glTF inverse-bind used directly as `m2b`), the material factor/channel parsing, the occlusion and
normal-map formulas, and the model-pool/clone/OGF-safety integration are all **verified correct**.

But the audit surfaced three classes of problem:

1. **Silent-corruption gaps** — assets that *should* load now produce garbage or wrong geometry with
   no error: **sparse accessors**, **Draco/meshopt compression**, **missing-tangent normal maps**, and
   the absence of an **`extensionsRequired` front-door gate**. These are the highest priority because
   they fail *silently* and several are cheap to at least *detect-and-reject*.
2. **Visible correctness gaps** on specific (common) assets: **doubleSided**, **factor-only metals**,
   the **skinned collision-box offset**, **default-scene** handling, **auto-scale** masking real scale,
   **MASK baseColorFactor.a**, **samplers (wrap)**, **TEXCOORD_1**.
3. **Missing pillars** (roadmap, not bugs): **animation playback** (the big one), **morph targets**,
   **KTX2/Basis**, non-triangle primitives, advanced KHR materials.

A separate set of gaps is **architecturally blocked** by the deferred + gamma + FP16 engine design
(true dielectric Fresnel, tinted SSR, sRGB albedo, HDR emissive bloom, transparent-in-G-buffer) — these
are correctly approximated already and a 1:1 fix would require renderer surgery that violates stock-safety.

---

## TIER 1 — Silent-corruption / correctness bugs (do first; mostly cheap)

### 1.1 `extensionsRequired` is never checked → silent garbage
- **Spec:** A loader MUST fail if any `extensionsRequired` entry is unsupported.
- **Us:** `gltf->extensions_required` is parsed by cgltf but never read. A Draco/meshopt/Basis-*required*
  asset loads as garbage/placeholder with no clear diagnostic.
- **Fix (LOW effort, OGF-safe, highest value):** after `cgltf_parse`, walk `extensions_required[]`
  against a small allowlist (`KHR_materials_emissive_strength`, `KHR_texture_transform`,
  `KHR_mesh_quantization`, and any we add); any unknown → `Msg("! [gltf] requires unsupported ext '%s'")`
  + `return false`. This single gate protects geometry (Draco), textures (Basis), etc. at once.
  Sites: `FExternalVisual.cpp` after each `cgltf_parse` (~`:629`, `:449`, `:1309`).

### 1.2 Sparse accessors return zeros → degenerate mesh (DOC FALSELY CLAIMS ✅)
- **Spec:** sparse accessor = base array + override list; effective value applies the overrides.
- **Us:** `cgltf_accessor_read_float/_uint/_index` **explicitly bail on `is_sparse`** (return 0 —
  `cgltf.h:2357/2501/2521`), and we **drop the bool return everywhere**, so POSITION stays `(0,0,0)` →
  mesh collapses to origin, silently. `docs/GLTF_SPEC_COVERAGE.md:30,111` wrongly says this works.
- **Fix (LOW–MED):** either switch attribute reads to `cgltf_accessor_unpack_floats` (which *does* apply
  sparse) into a temp array, OR detect `accessor->is_sparse` and reject+log. Also correct the doc.

### 1.3 Draco / meshopt compression → silent corruption
- **Spec:** geometry/buffers are compressed; client must run draco/meshoptimizer decoders. cgltf parses
  the metadata but does NOT decompress (compiled plain, `cgltf_impl.cpp`).
- **Us:** accessors read compressed bytes as floats → garbage or empty geometry, no detection.
- **Fix:** covered by 1.1's `extensionsRequired` gate for the *required* case; additionally guard
  `primitive.has_draco_mesh_compression` / `buffer_view.has_meshopt_compression` and reject+log.
  Full support later (meshoptimizer first — small lib; Draco — large C++ dep).

### 1.4 Missing tangent generation → wrong normal-mapped lighting
- **Spec:** when TANGENT is absent and a normalTexture exists, generate tangents (MikkTSpace) from
  positions/normals/UVs.
- **Us:** absent TANGENT → constant `T=(1,0,0)`, `B=cross(N,T)` (`FExternalVisual.cpp:736,762`). The bump
  shader (selected on *normal-texture presence*) then samples against a UV-unaligned frame → mis-lit bumps.
- **Fix (MED):** vendor MikkTSpace.c (public-domain, the spec reference) or a per-triangle UV-gradient
  fallback; run pre-bake (before node-world + Z-flip), only when a normal map is present.

---

## TIER 2 — Visible correctness gaps on common assets (medium effort, stock-safe)

### 2.1 `doubleSided` ignored → foliage/cloth/thin geo vanish from behind
- We always render single-sided (default cull); `doubleSided` is never read. **Fix:** material-driven
  no-cull `.s` variant + back-face normal flip via `SV_IsFrontFace` in the PS. (Our `.s`/PS only.)

### 2.2 Factor-only metals/roughness ignored → untextured metal props look like plastic
- Shader selection keys on the *presence* of an MR texture, so `metallicFactor`/`roughnessFactor` with
  no MR map fall to the flat shader and are dropped; the metal overlay never engages.
- **Fix (MED):** when `has_pbr_metallic_roughness` but no MR texture, bind a 1×1 **white MR** stand-in
  (mirror the existing `ext_white_texture_name` pattern) and select `external_mr`. Closes this *and* the
  factor-only-metalness miss in one change.

### 2.3 Skinned collision box offset (root bone non-identity)
- The model-space box is placed on the skeleton root bone; when that joint has a non-identity bind
  (CesiumMan-style stand-up rotation) the physics shell double-transforms it → offset/tilted collider.
- **Fix (MED):** always carry the box on a guaranteed-identity **synthetic root** (extend the existing
  multi-root synth logic to all skinned models), OR express the box in `inverse(rootBoneWorld)` space.
  Static/rigid path is correct.

### 2.4 Default scene ignored → over-includes orphan/multi-scene geometry
- We iterate the flat `gltf->nodes[]`; `gltf->scene`/`scenes[]` are never referenced.
- **Fix (LOW):** recurse the default scene's node tree; fall back to `scenes[0]`, then the flat loop only
  if `scenes_count==0`. (Same `cgltf_node_transform_world`, just the right *set* of nodes.)

### 2.5 Auto-scale: per-child desync + masks correct scale
- `EXTERNAL_AUTOSCALE` clamps the largest bbox dim into `[0.5,2.0]` **per child**, so multi-material
  models can get different scale factors per submesh (tearing), and correctly-authored meter-scale assets
  get rescaled.
- **Fix (MED):** compute ONE model-wide factor (in `FExternalKinematics`) and pass it to every child.
  Then make the band a *fallback for absurd sizes only* (e.g. >50u / <0.01u), honoring real meters by
  default (glTF is meters ≈ X-Ray units).

### 2.6 MASK alpha ignores `baseColorFactor.a`
- Spec: `alpha = baseColorTexture.a × baseColorFactor.a`, compared to `alphaCutoff`. We clip on texture
  alpha only. **Fix (TRIVIAL):** push `baseColorFactor.a` into `ext_base_color.w` (hardcoded 1.0 today)
  and `base.a *= ext_base_color.w` before `clip`.

### 2.7 Samplers (wrap/filter) ignored → edge bleed on atlases/decals
- All glTF textures forced to engine-global `WRAP` + aniso; `CLAMP_TO_EDGE`/`MIRRORED_REPEAT` assets
  show seams. **Fix (MED):** add named sampler states (clamp/mirror) and select per-material from
  `cgltf_sampler->wrap_s/_t`. Filter modes are lowest priority.

### 2.8 TEXCOORD_1 / per-texture `texCoord` ignored
- Only UV0 is read; the per-texture `texCoord` index is never inspected, so UV1-mapped textures
  (commonly occlusion/lightmaps) sample wrong coordinates. **Fix (MED-HIGH):** add a 2nd UV channel to
  `vertExternal` + decl + shaders, route each map per its `texCoord`. Cross-cuts geometry + shaders.

---

## TIER 3 — Fidelity / completeness (lower priority, mostly cheap)

- **KHR_texture_transform rotation** (we do offset+scale only, single transform for all maps): add
  sin/cos to `ext_uv`. Rotation is the high-value cheap win; per-texture transforms need per-map
  constants. *(Materials)*
- **Vertex colors only on the no-texture path** — multiply `COLOR_0` into albedo in the textured PSes too
  (element already present in the decl). *(Materials)*
- **>4 joint influences silently truncated** — detect `JOINTS_1/WEIGHTS_1`, warn, and select the 4
  largest weights before renormalizing (we keep set-0's first 4). Hard cap stays 4 (`vertBoned4W`). *(Skinning)*
- **Dielectric gloss cap** — `gloss = saturate((1-rough)² · lerp(k,1,metallic))` to curb smooth-dielectric
  over-reflection (the #1 visible material artifact). Couples highlight (single channel, unavoidable). *(Materials)*
- **Image dedup** — we re-decode shared images per material/child (and decode albedo+MR twice for metal
  materials). Key `$user$` by glTF **image index** and cache the parsed `cgltf_data` across a model's
  children. Memory/load-time only, not correctness. *(Textures)*
- **base64 `data:` URI images** — ~20 lines (cgltf already decodes base64 buffers; the image path just
  needs the same call). Rare. *(Textures)*
- **AO on non-MR shaders** — wire `s_ao`+lerp into `_flat.ps`/`_bump.ps` for the rare AO-without-MR case. *(Materials)*
- **KHR_materials_unlit** — route to a forward unlit pass (albedo out, no lighting), reuse blend/emissive
  plumbing. Cheap, common-ish. *(Materials)*
- **Negative-/non-uniform-scale nodes** — reverse winding per-node when `det(Mw)<0`; use inverse-transpose
  for normals under non-uniform scale. Fixes mirrored-instance inside-out + normal shear. *(Scene)*
- **Non-triangle primitives** — convert TRIANGLE_STRIP/FAN to a list at load (~30 lines); log skipped
  lines/points. *(Geometry)*
- **EXT_mesh_gpu_instancing** — expand instanced nodes during the node walk if such assets appear. *(Scene)*

---

## TIER 4 — Missing pillars (roadmap)

### Animation playback (THE big gap — bind pose only today)
- **Design (per the skinning audit):** parse `cgltf_animation` channels → map `target_node` to our bone
  via the already-captured `ExtBone::gltf_node`; sample TRS at playback time (binary-search keyframes;
  **LINEAR with quaternion slerp** + STEP first, CUBICSPLINE next); compose the sampled local TRS,
  conjugate by `C`, and drive the bones, then `CalculateBones` (already forced per frame — the hook is
  ready and forward-compatible).
- **CRITICAL architectural note:** `bind_transform` lives on the **shared** `CBoneData` — writing animated
  locals there makes ALL instances share one pose. Per-instance animation MUST go through
  **`CBoneInstance` callbacks** (`bi.set_callback`, `callback_overwrite()` at `SkeletonCustom.cpp:167-176`),
  NOT `bind_transform` mutation. **Design around bone-instance callbacks from the start.**
- Effort: LINEAR+STEP+looping ≈ 2–4 days (the hard parts — coordinate conversion, the bone chain — are
  already solved). CUBICSPLINE +1 day.

### Morph targets
- `mesh.primitives.targets` + `mesh.weights` not read. Needs CPU re-skin or a morph VS; no native X-Ray
  path. Defer until after skeletal animation. High effort, low near-term value.

### KTX2 / Basis (`KHR_texture_basisu`) + WebP
- D3DX11 can't decode KTX2/WebP → placeholder. Rising prevalence (standard GPU-compressed glTF delivery).
  Real fix = link libktx/basis_universal, transcode to BC, build the texture directly. High effort.

### Advanced KHR materials
- clearcoat/transmission/volume/sheen/anisotropy/iridescence/ior/specular — need new BRDF lobes and/or
  shared-combine access. Long tail; even mature engines partial-support these. (Unlit is the cheap one,
  see Tier 3.)

---

## Architecturally BLOCKED by the engine (accept — already well-approximated)

| Gap | Why blocked |
|---|---|
| True dielectric F0=0.04 Fresnel (proper reflection-strength vs our gloss curve) | Reflection strength decided in the **shared deferred combine**; gloss couples strength+highlight in one channel. (The "no dielectric Fresnel cap" diagnosis is *inferred* from the single-channel gloss + shared-combine structure, not read line-by-line in the un-shipped combine.) |
| Per-pixel metal flag in G-buffer | Position RT is **FP16** (*verified* `D3DFMT_A16B16G16R16F`); the material-ID `&31` / all-slots-swept sub-claim is *inferred* (rests on the un-shipped base-pack `common.h`). Either way, no free bit. (This is *why* metalness is a forward overlay.) |
| Tinted SSR for metals | SSR computed in the shared combine; overlay tints only the cubemap. |
| Roughness-based reflection blur | No mip chain on the engine sky cubes. |
| sRGB-correct baseColor/emissive | Whole engine is a **gamma pipeline** (stock albedo non-sRGB UNORM, no linearization). UNORM decode is the right *consistency* call; 1:1 sRGB would make only glTF surfaces darker than stock. |
| HDR emissive bloom | LDR additive onto a tonemapped scene; bloom needs hooking `phase_combine` or `dx11_hdr10`. |
| BLEND point lights / shadows / SSR / per-triangle sort | A transparent surface can't be in the G-buffer; renders forward post-combine. Fundamental to deferred (stock glass identical). |

---

## Confirmed CORRECT — do not touch

- **RH→LH conversion**: the `C=diag(1,1,-1)` Z-flip + winding reversal (static) and `C·M·C` conjugation
  (skinned, on world bind AND IBM) — mathematically the correct general transform, validated.
- **Node-world bake + memcpy convention**: `cgltf_node_transform_world` is byte-identical to upstream cgltf;
  the column-major→row-major reinterpretation + `transform_tiny` (`v·M`) is the correct transpose cancel.
- **Skinning**: glTF inverse-bind used directly as `m2b` (handles bind-shape offsets the hierarchy inverse
  drops); synthetic single root; the `skinning=4` member → `SetShaderTexture` → `SetSkinningMode` → SKIN_4
  variant + dedicated `external_skinned.s`; forced per-frame `CalculateBones` (forward-compatible w/ animation).
- **Materials**: normalScale formula, occlusion formula+channel (R), emissive factor×strength, MASK cutoff
  mechanics, baseColorFactor-only (white×factor), metallic/roughness **channel** mapping (G=rough,B=metal).
- **Textures**: full mip-chain generation, normal/MR/AO **linear** decode (spec-correct), white-for-factor-only
  + crash-safe placeholder, `$user$` no-disk binding.
- **Geometry**: interleaved/byteStride, normalized-integer/COLOR_0 dequant, 32-bit indices, per-material
  merge, KHR_mesh_quantization (dequantized via normalized accessor path).
- **Integration**: clone-by-Type, cache-key/extension preservation, stock OGF/OMF behavior unchanged (additive, gated), lights/cameras
  intentionally ignored (correct for a prop loader).

---

## Doc hygiene (stale/wrong claims to correct)
- `GLTF_SPEC_COVERAGE.md:30,111` — **sparse accessors are NOT supported** (claimed ✅). Fix.
- `GLTF_SPEC_COVERAGE.md:58-59,110` — skinning is now **bind-pose-shipping** (claimed ❌). Update.
- `FExternalVisual.h:63` — the `m2b == CalculateM2B (cross-check)` comment is misleading; IBM is used
  directly and is deliberately NOT the hierarchy inverse.
- `FExternalVisual.cpp:1487` — the 64-bone limit's true cause is the **u64 visimask**, not the "78-row
  sbones array"; fix the comment.

---

## Recommended order of attack (max conformance per unit effort)
1. **`extensionsRequired` gate** + **sparse guard** + **Draco/meshopt detect-reject** — kill silent
   corruption; turn garbage into clean logged failures. *(All LOW, one afternoon.)*
2. **MASK baseColorFactor.a** + **vertex colors on textured paths** + **KHR_texture_transform rotation**
   + **AO on non-MR** — trivial shader/loader wins. *(LOW.)*
3. **Factor-only metals (white MR stand-in)** + **doubleSided** + **default scene** + **auto-scale
   single-factor** — the highest-value visible-correctness fixes. *(MED.)*
4. **Tangent generation (MikkTSpace)** + **skinned collision box** + **samplers (wrap)** + **TEXCOORD_1**.
   *(MED.)*
5. **Animation playback** (via bone-instance callbacks) — the big pillar. *(Multi-day.)*
6. Long tail: KTX2/Basis, morph targets, non-triangle prims, advanced KHR materials, base64 URIs.
