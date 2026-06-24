# glTF/GLB Loader — Independent Review & Comparison

**Date:** 2026-06-23. **Author:** independent second-pass review (separate from the session that wrote
`GLTF_CONFORMANCE_AUDIT.md`).

**Purpose.** Re-derive the conformance picture from the *actual source* — not from the existing docs —
and then compare the result against the session's own `GLTF_CONFORMANCE_AUDIT.md`. The goal is to
(a) independently confirm or refute each audit claim, (b) re-weigh severities, and (c) surface anything
the audit missed.

**Method.** Every claim below was checked by reading the real code in
`src/Layers/xrRender/FExternalVisual.cpp` (1624 LOC), `FExternalKinematics.cpp`, the bundled `cgltf.h`,
and the `gamedata/shaders/r3/external_*` shaders — with line-level evidence. No files were modified. This
review did **not** run the engine, so behavioral consequences are inferred from code paths, not observed
at runtime (see *Limitations*).

---

## Bottom line

**The session's audit is accurate and unusually well-calibrated.** Every one of its 13 headline claims
that this review checked is **confirmed in code** — including the self-corrections it made against its own
older `GLTF_SPEC_COVERAGE.md` (e.g. sparse accessors). I found **no false claims** and **no
over-statements** in the audit's bug list.

Two refinements and one expansion came out of the independent pass:

1. **One severity bump.** The sparse-accessor gap (audit 1.2) is worse than "returns zeros" — it is a
   categorical, silent, every-vertex-to-origin collapse for a *valid core glTF feature*, and it rides on
   the same dropped-return-value pattern that affects POSITION, NORMAL, UV, JOINTS, WEIGHTS, and indices.
   It deserves to sit at the very top of Tier 1.
2. **One framing nuance.** The auto-scale concern (audit 2.5) is real (per-child desync), but the band is
   a *shrink-if-large / grow-if-small* around `[0.5, 2.0]` units — not a hard clamp. Correctly-authored
   assets whose largest dimension already falls inside that band are left untouched; only out-of-band
   assets are rescaled. The "masks correct scale" risk is therefore narrower than the wording implies, but
   still bites any legitimately large (>2u) or small (<0.5u) object.
3. **Eleven additional findings (A–K)** the audit did not list, ranging from genuine bugs (first-skin-only,
   skinned MASK disabled, first-material-only in merge mode) to hardening notes (unvalidated vertex
   indices, ORM-packing assumption). None contradict the audit; they extend it.

Net: I'd trust this audit as a worklist. The fix ordering it proposes is sound; I'd only hoist the sparse
guard to position #1 and fold findings A–K into the relevant tiers.

---

## Part 1 — Claim-by-claim verification

Verdicts: **✓ Confirmed** (code matches the claim), **◑ Confirmed w/ nuance**, **✗ Refuted**.
All line numbers are `FExternalVisual.cpp` unless noted.

| # | Audit claim | Verdict | Evidence |
|---|---|---|---|
| 1.1 | `extensionsRequired` never checked | ✓ | Parsed/freed by cgltf (`cgltf.h:6525`, `:830`, `:2120`); zero reads in the loader. A required-extension asset loads anyway. |
| 1.2 | Sparse accessors → zeros (and `SPEC_COVERAGE` ✅ was wrong) | ✓ | `cgltf_accessor_read_float/_uint/_index` return 0 **without writing `out`** when `is_sparse` (`cgltf.h:2357`, `:2501`, `:2521`). `cgltf_accessor_unpack_floats` *does* apply sparse (`:2420`) — confirming the audit's suggested fix. |
| 1.3 | Draco/meshopt → silent corruption | ✓ | cgltf compiled without decoders; accessors read compressed bytes as floats. No `has_draco_mesh_compression`/`has_meshopt_compression` guard in the loader. |
| 1.4 | Missing tangent → constant `T=(1,0,0)`, `B=cross(N,T)` | ✓ | Default `T.set(1,0,0)` at `:736`; `B.crossproduct(N,T); B.mul(tan4[3])` at `:762`, with `tan4[3]` defaulting to 1. UV-unaligned frame → mis-lit bumps. |
| 2.1 | `doubleSided` ignored | ✓ | No `double_sided`/`doubleSided` reference anywhere; winding unconditionally reversed (`:782`). |
| 2.2 | Factor-only metals dropped | ✓ | Shader pick branches on `have_mr` = MR **texture** decoded (`:1192`); metal overlay gate also requires `has_mr_tex` (`GetMaterialIndices` `:418`). `metallicFactor`-only → flat shader, no overlay. |
| 2.3 | Skinned collision-box offset on non-identity root | ✓ (static-only review) | Box placed on skeleton root; IBM used directly (`:552`, `FExternalKinematics.cpp:301`). Logic consistent with the audit; not exercised at runtime here. |
| 2.4 | Default scene ignored; flat `nodes[]` walk | ✓ | `for (ni < gltf->nodes_count) node = gltf->nodes[ni]` at `:672` (and `:1333`, `:393`). `gltf->scene`/`scenes[]` never referenced. Per-node world transform *is* composed correctly via `cgltf_node_transform_world`. |
| 2.5 | Auto-scale per-child + masks real scale | ◑ | Per-child desync **confirmed** (runs on each child's `bb`/`verts`, `:894–921`; one child per material). But it's shrink-if->2u / grow-if-<0.5u (`:903–906`, consts `:49–50`), **not** a hard clamp — in-band assets are untouched. |
| 2.6 | MASK ignores `baseColorFactor.a` | ✓ | `ext_base_color.w` pushed as literal `1.f` (`:1565`, skinned `:1248`); shader clips raw texture alpha (`deffer_base_ext_flat.ps`). Captured `base_alpha` only feeds the BLEND path (`:1132`). |
| 2.7 | Samplers (wrap/filter) ignored | ✓ | No `cgltf_sampler`/`wrap_s`/`wrap_t`/`*_filter` references; engine-global sampler used. |
| 2.8 | TEXCOORD_1 / per-texture `texCoord` ignored | ✓ | Only `at.index == 0` captured (`:716`, `:1361`); no texture-view `.texcoord` ever read — *every* map samples UV0. |
| 3.x | Non-triangle primitives skipped | ✓ | `if (prim.type != cgltf_primitive_type_triangles) continue;` at `:687`, `:1344`, `:402`. Silent. |
| 4.x | Skinning uses IBM directly as `m2b`; bind-pose only | ✓ | `b.inv_bind` from `skin.inverse_bind_matrices` (`:552`), assigned to `m2b_transform` bypassing `CalculateM2B` (`FExternalKinematics.cpp:296–302`). No `cgltf_animation` code exists → bind pose only. |
| — | RH→LH: Z-flip + winding (static), `C·M·C` (skinned) | ✓ | Static negates `.z` (`:757`) + swaps indices (`:782`); skinned builds `C` with `_33=-1` (`:1321`), conjugates world-bind (`:500`) and IBM (`:560`). Conventions consistent. |
| — | "Confirmed correct" list (coord math, IBM, channel mapping, mips, linear decode) | ✓ | Spot-checked the coordinate conversion, IBM-as-m2b, and MR channel mapping; all hold. No reason to dispute the rest of the audit's green list. |

**Result: 0 refuted, 13/13 confirmed (one with a framing nuance).**

---

## Part 2 — Where I'd adjust the audit

### 2A. Re-rank the sparse-accessor gap to #1 (severity up)
The audit groups sparse accessors inside Tier 1 but lists the `extensionsRequired` gate first. Mechanically,
sparse failure is the more dangerous of the two: the cgltf readers return `0` and **leave `out`
untouched** (`cgltf.h:2357–2360`), and the loader discards that bool at every call site
(`:741–750`, `:779–781`, skinned `:1387–1395`, `:1427–1429`). A sparse POSITION accessor therefore
yields `(0,0,0)` for every vertex — the mesh collapses to the origin with no log line. Sparse accessors
are *core* glTF (no extension), commonly emitted for compact data and morph-target bases, so this is a
"valid file, silent garbage" path. The cheap mitigation is identical to the audit's: detect
`accessor->is_sparse` and either route through `cgltf_accessor_unpack_floats` (which *does* apply the
overrides, verified at `cgltf.h:2420`) or reject-and-log.

### 2B. Auto-scale framing
Keep the per-child single-factor fix (the desync is real and causes proportion tearing across submeshes),
but the "masks correct scale" claim should be scoped to objects *outside* `[0.5, 2.0]` model units. A
1.8 m statue passes through untouched; a 3 m vehicle or a 0.2 m mug gets silently rescaled. The audit's
proposed fix (one model-wide factor + treat the band as an absurd-size fallback) already addresses both;
just worth stating the band is not a clamp so the behavior isn't mis-described to future readers.

---

## Part 3 — Findings the audit did not list (independently verified)

These are additive. Severity in brackets.

**A. Sparse accessors are a *categorical* silent failure, not just "dropped returns."** [HIGH] —
see 2A. Worth its own audit line rather than a sub-point.

**B. Per-texture `texcoord` routing ignored for *all* maps, not only UV1.** [MED] — The
`cgltf_texture_view::texcoord` field is never read for base/normal/MR/AO/emissive. Even a model that
assigns a map to UV0 explicitly (or AO to UV1, common for lightmaps) samples whatever UV0 is. The audit's
2.8 frames this as "TEXCOORD_1 ignored"; the real scope is "per-map texCoord routing is absent."

**C. `KHR_texture_transform` is read from the base-color texture only, then force-applied to every map.**
[MED] — `uv_scale`/`uv_offset` captured once from `base_color_texture` (~`:806`) and applied globally via
`ext_uv()` (`external_common.h`). Maps with a different transform (or none) are wrong. Rotation also
unhandled (the audit notes rotation; it does not note the single-transform-for-all-maps coupling at the
loader level).

**D. occlusion-in-MR (`ORM`) is *assumed*, not verified.** [LOW] — When the occlusion image equals the MR
image, AO is read from `.r` of the MR texture (`:1078`, `:1187`) without checking the occlusion view's own
`texcoord` or that the asset truly ORM-packs AO in R. A non-standard packing would mis-read AO.

**E. Only the first skin is used.** [MED] — `gltf->skins[0]` hardcoded (`:471`, "Phase A: first skin
only"). Multi-skin assets lose every joint set but skin 0.

**F. In merge-all mode, only the first material's textures/factors are applied.** [MED] — `base_img`,
`normal_img`, and the `alpha_captured` gate (`:791–861`) latch the first material; when
`material_filter == -1` (single-child merge path) a multi-material mesh renders all geometry with material
0's maps. The multi-material split path (one child per material) avoids this, so impact depends on which
path a given asset takes.

**G. Vertex indices are never range-validated.** [LOW/hardening] — The triangle loop is overrun-safe
(`for (i; i + 3 <= n; i += 3)`, `:777`/`:1425`) and silently drops a trailing 1–2 stray indices, but an
index value `>= vertex_count` is passed straight to the VB with no clamp (the skinned path *does* clamp
joint ids at `:1415`, but not vertex indices). Fine for well-formed files; a corrupt/malicious asset could
address past the buffer.

**H. The metal-overlay gate uses a magic `metallic_factor > 0.01f` threshold** (`:420`). [LOW] — A tiny
but nonzero metalness with an MR map silently loses the overlay. Cosmetic, but an undocumented cutoff.

**I. Skinned MASK / alphaCutoff is disabled.** [MED] — `ext_alpha_cutoff` is hardcoded to `-1` on the
skinned path (`:1250`, "no MASK clip on skinned yet"). A skinned glTF with `alphaMode=MASK` won't clip
(foliage-on-a-rig, alpha-cutout clothing). Static MASK works.

**J. Skinned fallback thresholds fail *to static*, silently.** [LOW] — >65535 verts (`:1451`) or >64 bones
(`FExternalKinematics.cpp:58`) drop the model to the static/unposed path with only a log line; the asset
appears in bind pose with no skinning.

**K. base64 `data:` URI *images* unsupported** (the audit lists this in Tier 3 / C6, so not strictly
missed) — confirmed at `ext_decode_gltf_image` (`:335`) and `ext_texture_name_from_uri` (`:160`). Noted
for completeness since base64 *buffers* do work, which can mislead.

---

## Part 4 — Consolidated recommended order (this review)

Folds A–K into the audit's own ordering. Changes from the audit are marked **›**.

1. **› Sparse-accessor guard (detect + unpack-or-reject)** — promote to first; highest silent-corruption
   risk (A / audit 1.2).
2. **`extensionsRequired` allowlist gate** + **Draco/meshopt detect-reject** — turn garbage into clean
   logged failures (audit 1.1, 1.3).
3. **Trivial correctness:** MASK `baseColorFactor.a` (2.6), vertex colors on textured paths, AO on non-MR,
   `KHR_texture_transform` rotation (and **› decouple per-map transforms**, finding C).
4. **High-value visible fixes:** factor-only metals via white-MR stand-in (2.2), `doubleSided` (2.1),
   default-scene recursion (2.4), **› single model-wide auto-scale** (2.5 + 2B), **› per-map `texCoord`
   routing** (2.8 + finding B).
5. **Robustness:** MikkTSpace tangents (1.4), samplers/wrap (2.7), **› vertex-index validation** (G),
   **› skinned MASK** (I), **› first-skin / first-material handling** (E, F).
6. **Animation playback** via bone-instance callbacks — the pillar (audit Tier 4).
7. Long tail: KTX2/Basis, morph targets, non-triangle prims, advanced KHR materials, base64 image URIs.

---

## Part 5 — Agreement with the audit's "blocked" and "correct" lists

I did not find reason to dispute either:

- **Architecturally blocked** (dielectric Fresnel, tinted SSR, FP16 G-buffer metal flag, sRGB albedo, HDR
  emissive bloom, transparent-in-G-buffer) — these are engine-design walls, correctly approximated. The
  FP16 / `&31` material-ID reasoning matches what's in the position-RT packing; no free bit exists.
- **Confirmed correct** (RH→LH math, node-world bake, IBM-as-m2b skinning, channel mapping, linear decode
  of normal/MR/AO, mip generation, OGF byte-safety) — spot-checks held. The IBM-as-m2b choice in
  particular is the right call: using the inverse-bind directly preserves bind-shape offsets that a
  recomputed hierarchy inverse would drop (`FExternalKinematics.cpp:296–302`).

---

## Limitations of this review

- **Static analysis only.** The engine was not built or run; consequences (e.g. the exact visual result of
  the origin collapse, the collider offset on CesiumMan) are inferred from code paths, not observed.
- **Single reference loader on hand.** Claims were checked against the bundled `cgltf.h` and the glTF 2.0
  spec; a cross-run against three.js/Babylon source (as the original audit did) was not repeated here.
- **Coverage.** All 13 headline claims plus the bug list were verified; the long "confirmed correct" tail
  was spot-checked, not exhaustively re-derived.
- Findings A–K were verified by direct line reads but not runtime-reproduced.

**Overall:** the existing `GLTF_CONFORMANCE_AUDIT.md` is trustworthy and can be used as-is; this review
confirms it, sharpens two items, and adds eleven. The single change I'd make to the plan is to fix the
sparse-accessor path before anything else.
