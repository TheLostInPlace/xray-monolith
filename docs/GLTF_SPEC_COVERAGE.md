# glTF 2.0 Spec Coverage — External Models in X-Ray Monolith

A feature-by-feature breakdown of the glTF 2.0 specification against what the external-model loader
(`FExternalVisual` / `FExternalKinematics` + the `gamedata/shaders/r3/external_*` shaders) actually
supports. Everything here is implemented **alongside** the stock OGF/OMF pipeline; stock OGF/OMF
behavior is unchanged — the glTF integration is additive and gated.

Companions: `docs/GLTF_GLB_Integration_Research.md` (architecture), `docs/GLTF_REVISIT.md` (parked items
and engine limitations).

Last updated: 2026-06-23.

**Legend**
- ✅ **Implemented** — works to spec for the common cases.
- ⚠️ **Partial / not fully proper** — works but with caveats, approximations, or unhandled sub-cases.
- ❌ **Not implemented** — absent; the feature is ignored or the asset won't load.
- ➖ **N/A** — not applicable to a game engine integration (the engine owns this concern).

---

## 1. Container & format

| Feature | Status | Notes |
|---|---|---|
| `.glb` (binary) | ✅ | Loaded via cgltf; JSON + BIN chunk. |
| `.gltf` (JSON) + external `.bin` | ✅ | `cgltf_load_buffers` resolves the `.bin` relative to the file path. |
| Base64 `data:` URI **buffers** | ✅ | cgltf decodes embedded base64 buffers. |
| Buffers / bufferViews / accessors | ✅ | Read via `cgltf_accessor_read_float` / `cgltf_accessor_read_index`. |
| Sparse accessors | ❌ | **Rejected with a log**, not loaded. cgltf's `cgltf_accessor_read_*` silently return 0 for a sparse accessor (would collapse the mesh to the origin), so the loader detects `is_sparse` on any consumed vertex/index/IBM accessor up front and refuses the model (`! [gltf] ... sparse accessor`). |
| Read through the engine VFS | ✅ | Loaded via `FS.r_open`, so loose **and** packed (.db) assets work. |

---

## 2. Scene graph

| Feature | Status | Notes |
|---|---|---|
| Nodes | ✅ | All nodes carrying a mesh are emitted. |
| Node transforms (TRS **and** matrix) | ✅ | `cgltf_node_transform_world` baked into vertex positions/normals/tangents at load. |
| Node hierarchy | ✅ | World transform (full parent chain) is baked per node. |
| Right-handed → left-handed conversion | ✅ | `EXTERNAL_FLIP_Z` + winding reversal so front faces face out. |
| Multiple scenes / active-scene selection | ⚠️ | We iterate **all** mesh-bearing nodes regardless of which `scene` they belong to; the `scene` property isn't respected (rare to matter). |
| `KHR_node_visibility` | ❌ | Node visibility toggling not read. |

---

## 3. Mesh geometry & attributes

| Feature | Status | Notes |
|---|---|---|
| Triangle primitives | ✅ | `cgltf_primitive_type_triangles`. |
| Points / lines / strips / fans | ❌ | Non-triangle primitives are skipped (no geometry emitted). |
| `POSITION` | ✅ | |
| `NORMAL` | ✅ | Defaults to up-normal if absent. |
| `TANGENT` | ✅ | Bitangent derived with the glTF handedness sign. **Absent TANGENT + a normal map → tangents are generated** (per-triangle UV-gradient, MikkTSpace-lite) instead of a constant T. |
| `TEXCOORD_0` | ✅ | |
| `TEXCOORD_1` (2nd UV set) | ✅ | UV1 is read and each map (base/normal/MR/AO) samples UV0 or UV1 by its glTF `texCoord` on the static lit paths. (KHR_texture_transform still applies to UV0 only; metal/blend/emissive overlays + skinned use UV0.) |
| `COLOR_0` (vertex colours) | ✅ | Multiplied into albedo on the static lit paths (flat / MR / bump / bump+MR / vertex-colour). The skinned path and the metal/blend/emissive overlays still ignore it. |
| `JOINTS_0` / `WEIGHTS_0` (skinning) | ❌ | No skinning — static bind pose only. |
| Morph targets | ❌ | Not read. |
| 16- and 32-bit indices | ✅ | 32-bit path added for >65535-vertex meshes (engine hardcodes R16, rebound to R32 per-draw). |
| Multi-primitive / multi-material meshes | ✅ | One render child per material. |

---

## 4. Materials — metallic-roughness PBR (core)

| Feature | Status | Notes |
|---|---|---|
| `baseColorTexture` | ✅ | Decoded from GLB bufferView or external file. |
| `baseColorFactor` (RGB tint) | ✅ | Multiplies albedo. |
| `baseColorFactor.a` | ✅ | BLEND opacity multiplier **and** applied to the MASK alpha test (texel alpha `*= baseColorFactor.a` before `clip`). |
| `metallicRoughnessTexture` | ✅ | G→roughness, B→metallic. |
| `metallicFactor` / `roughnessFactor` | ✅ | Multiply the sampled values. |
| **Real metalness** (colored reflection, ~no diffuse) | ⚠️ | Implemented in our **own** forward pass (reflects the engine sky cubes, tinted by albedo, roughness-aware). Stock-safe but **not** physically-tinted SSR. `metallicFactor`-only metals (no MR texture) now render metallic via a 1×1 white MR stand-in (constant metalness from the factor). See `GLTF_REVISIT.md` A2/B2. |
| `normalTexture` | ✅ | Standard tangent-space normal map. |
| `normalTexture.scale` (normalScale) | ✅ | Scales the tangent XY. |
| `occlusionTexture` + `strength` | ✅ | Modulates the ambient/hemi term. Handles **ORM** (occlusion in the MR texture's R) **and** a separate AO texture. |
| AO on non-MR materials | ⚠️ | Only the MR shaders apply AO. A material with a separate AO map but **no** MR texture won't get AO (rare). |
| `emissiveTexture` + `emissiveFactor` | ✅ | Forward additive overlay (added on top of the lit surface). |
| Emissive HDR bloom | ⚠️ | LDR additive only — values clamp at 1, no bloom halo (engine limitation, `GLTF_REVISIT.md` B1). |
| `alphaMode = OPAQUE` | ✅ | |
| `alphaMode = MASK` + `alphaCutoff` | ✅ | Per-material cutoff, deferred alpha-test (`clip`). Verified exact at .25/.50/.75. |
| `alphaMode = BLEND` | ⚠️ | Forward, back-to-front, src-alpha pass with simple sun+ambient lighting. Caveats: no point lights/shadows *on* the surface, per-object (not per-triangle) sort, doesn't cast shadows, not in SSR. |
| `doubleSided` | ❌ | Everything renders single-sided (back faces culled). Affects foliage/cloth. |

---

## 5. Textures, images, samplers

| Feature | Status | Notes |
|---|---|---|
| Image from external file URI | ✅ | Resolved relative to the glTF, percent-decoded. |
| Image from bufferView (GLB-embedded) | ✅ | Decoded from memory via D3DX11 (PNG/JPG/etc. + mips). |
| Image from base64 `data:` URI | ❌ | Not decoded (falls back to placeholder). Rare in shipped assets. |
| sRGB vs linear color space | ✅ | Resolved: X-Ray is a gamma pipeline, so base/emissive are correctly left UNORM (forcing sRGB would darken vs stock). |
| Texture sampler wrap modes (repeat/clamp/mirror) | ⚠️ | We bind a default linear/repeat sampler; per-texture wrap/filter from the glTF `sampler` isn't read. |
| Texture sampler filter / mip settings | ⚠️ | Default linear + mips from the decoder; the glTF sampler's min/mag/mip filters aren't honored. |
| `KHR_texture_transform` | ⚠️ | Offset + scale + **rotation**; one transform (the base-color texture's) applied to **all** maps rather than per-texture. |
| `KHR_texture_basisu` (KTX2 / Basis) | ❌ | Basis-compressed textures won't decode. |

---

## 6. Animation & skinning

| Feature | Status | Notes |
|---|---|---|
| Keyframe animation (node TRS) | ❌ | Static bind pose only. |
| Animation interpolation (LINEAR/STEP/CUBICSPLINE) | ❌ | — |
| Morph-target (weights) animation | ❌ | — |
| Skinning (skins, inverse-bind matrices, joints) | ❌ | The big remaining pillar. Would transcode into X-Ray's `CKinematics` / `CMotionDef` skeleton system. |
| `KHR_animation_pointer` | ❌ | — |

---

## 7. Cameras & lights

| Feature | Status | Notes |
|---|---|---|
| Cameras (perspective/orthographic) | ➖ | The engine owns the camera; glTF cameras are ignored by design. |
| `KHR_lights_punctual` | ➖ / ❌ | glTF-authored lights aren't imported; the scene's own lighting (sun + dynamic) lights the models. |

---

## 8. KHR extensions (ratified)

| Extension | Status | Notes |
|---|---|---|
| `KHR_materials_emissive_strength` | ✅ | `emissiveFactor × strength` respected. |
| `KHR_texture_transform` | ⚠️ | Offset+scale+rotation, one transform for all maps (see §5). |
| `KHR_mesh_quantization` | ⚠️ | Quantized attributes are dequantized by cgltf's accessor reads, so geometry loads; not explicitly validated. |
| `KHR_materials_unlit` | ❌ | Unlit materials render lit. |
| `KHR_materials_clearcoat` | ❌ | |
| `KHR_materials_sheen` | ❌ | |
| `KHR_materials_transmission` | ❌ | (Transmission test assets render opaque.) |
| `KHR_materials_volume` | ❌ | |
| `KHR_materials_ior` | ❌ | |
| `KHR_materials_specular` | ❌ | |
| `KHR_materials_anisotropy` | ❌ | |
| `KHR_materials_iridescence` | ❌ | |
| `KHR_materials_dispersion` | ❌ | |
| `KHR_materials_variants` | ❌ | Material switching not supported. |
| `KHR_draco_mesh_compression` | ❌ | No Draco decoder. **Detected and rejected with a log** (`has_draco_mesh_compression`) instead of loading as garbage. |
| `EXT_meshopt_compression` | ❌ | No meshopt decoder. **Detected and rejected with a log** (`has_meshopt_compression`) instead of loading as garbage. |
| `extensionsRequired` gate | ✅ | A required extension not on the allowlist (`KHR_materials_emissive_strength`, `KHR_texture_transform`, `KHR_mesh_quantization`) is rejected with a log — one front door covering Draco/Basis/meshopt at once. |
| `KHR_lights_punctual` | ➖ | See §7. |
| `KHR_node_visibility` | ❌ | |
| `KHR_xmp_json_ld` | ➖ | Metadata; not relevant to rendering. |

---

## 9. Engine-integration extras (beyond the spec)

Things we add to make glTF assets behave as first-class X-Ray objects (not glTF features, but part of
"what's implemented"):

- ✅ **Spawns + physics** — wrapped as a 1-bone rigid `CKinematics` with an `stBox` collision shape from
  the bounding box, so glTF models spawn as `physic_object`s.
- ✅ **Runtime texture decode from memory** (D3DX11) under synthetic `$user$` names — no disk round-trip.
- ✅ **Auto-scale** — clamps a model's largest dimension into a sane band so wrongly-scaled assets aren't
  comically large/small.
- ✅ **Skybox-correct deferred integration** — writes the lit-geometry stencil mark.
- ✅ **Sun shadows** — cast via a back-face depth-only pass (tuned against self-shadow acne).
- ⚠️ **Dynamic-light (flashlight/lamp) shadows** — **deliberately not cast** by props: a close light +
  thin geometry self-shadowed (the lantern post went black), a depth-bias problem with no `.s` knob.
  Props stay lit/visible and just don't block dynamic lights. See `GLTF_REVISIT.md` B5.

---

## 10. Summary

**Solidly implemented (static PBR props):** container (.glb/.gltf), scene-graph transforms, triangle
geometry with positions/normals/tangents/UV0, multi-material, the full metallic-roughness texture set,
real metalness, normal maps, occlusion (AO), emissive (factor×strength), **all three alpha modes**
(opaque/mask/blend), material factors, texture-transform (offset+scale), and vertex colours — plus
engine integration (spawn/physics/auto-scale/sun-shadows).

**Implemented but not fully proper (⚠️):** metalness (own forward reflection, not tinted SSR; needs an MR
texture), BLEND (simple forward lighting, not in SSR, no shadow), emissive (LDR, no bloom), texture-
transform (one transform for all maps; rotation supported, per-map decoupling not yet), vertex colours
(static lit paths; skinned/overlays not), AO (MR shaders only), texture samplers (wrap/filter ignored),
and dynamic-light shadows (deliberately off for props).

**Not implemented (❌):** **animation & skinning** (the biggest gap), morph targets, `doubleSided`,
non-triangle primitives, base64 data-URI images, KTX2/Basis & Draco compression, and the advanced KHR
material extensions (clearcoat, transmission, volume, ior, specular, sheen, anisotropy, iridescence,
dispersion, unlit, variants).

**Headline:** the **static, opaque/transparent, metallic-roughness prop** is essentially complete; the
**animation/skinning pillar** is the major remaining work, followed by a long tail of advanced material
extensions that even mature engines only partially support.
