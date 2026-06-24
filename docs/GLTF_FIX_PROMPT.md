# Hand-off Prompt — glTF/GLB Loader Conformance Fixes

> Paste everything below the line into your Opus 4.8 coding session. It is self-contained.
> Companion docs in this same folder: `GLTF_CONFORMANCE_AUDIT.md` (the original audit),
> `GLTF_INDEPENDENT_REVIEW.md` (independent verification + extra findings), `GLTF_SPEC_COVERAGE.md`,
> `GLTF_REVISIT.md` (parked engine-limited items — do not attempt these).

---

You are working on the X-Ray Monolith engine's **external glTF/GLB model loader**. Two conformance
reviews (`docs/GLTF_CONFORMANCE_AUDIT.md` and `docs/GLTF_INDEPENDENT_REVIEW.md`) have already been done and
independently cross-checked against the source. Your job is to **implement the prioritized fixes** below.
Read both docs first — they contain the full rationale and line-level evidence for every item.

## Absolute hard rule (read twice)
**Nothing you do may change how stock OGF/OMF or any existing stock content renders, spawns, or
collides.** All glTF work lives in its own files and its own `.s` shaders. Concretely:
- Only touch: `src/Layers/xrRender/FExternalVisual.{cpp,h}`, `FExternalKinematics.{cpp,h}`,
  `src/Layers/xrRender/cgltf.h` (only if a guarded behavior change is unavoidable — prefer guarding at
  the call site), and `gamedata/shaders/r3/external_*.{s,ps,vs,h}` + their mirror under
  `compressor/mod/shaders/r3/`.
- Do **not** edit shared blenders, the deferred combine, `phase_*`, stock `.s`/`.ps`, or any OGF path.
- If a "proper" fix would require shared-shader surgery, **stop and leave it** — it's already triaged as
  architecturally blocked in `GLTF_REVISIT.md`. Do not regress stock to chase glTF 1:1.

## How to run this — orchestration (read before you start)
You are the **lead/orchestrator**. Do not grind through this serially yourself — get leverage from
subagents, parallelism, and an adversarial verification pass. This is what makes the difference between a
"good enough" run and a clean one.

- **Recon first, in parallel.** Before writing any code, spawn read-only explore subagents to map the
  exact call sites for the phase you're about to do, and to fully read both review docs + the cited code.
  Fan them out in one batch (one per file/concern) and collect the line-level findings. Don't start
  editing from assumptions — start from a confirmed map.
- **Parallelize independent edits.** Items that touch disjoint files/shaders can run as concurrent
  subagents. Within a phase, group by file: e.g. Phase 2's PS-shader edits (2.1/2.2/2.4) are largely
  independent of the loader-side capture changes — split them. Serialize only where there's a real data
  dependency (e.g. the per-map `texCoord` work in 3.5 cross-cuts the vertex decl *and* every shader, so
  it owns that surface alone).
- **Always run a dedicated verification subagent after each phase.** A *separate* agent that did not write
  the code re-reads the diff against (a) the glTF 2.0 spec, (b) the relevant review doc claim, and (c) the
  hard rule below — and tries to break it. Treat its report as a gate: do not move to the next phase until
  it comes back clean. This adversarial second set of eyes is the single highest-value habit here; it
  catches the silent-regression class that one pass misses.
- **Spec each subagent tightly.** Give every subagent the file, the exact lines, the desired behavior, the
  hard rule, and the definition of done — the same level of specificity this prompt gives you. A precise
  brief substitutes for raw model headroom; a vague one wastes the run.
- **Small, reviewable commits.** One logical fix per commit, mirroring the git history style
  (`glTF: ...`, `docs: ...`). Build between commits. A reviewer should be able to read any single commit
  and understand it in isolation — this also makes a bad change trivial to bisect/revert.

## Working method
- Work in **phases** (below). After each phase: build, smoke-test the relevant asset(s), run the
  verification subagent, and **commit** before starting the next.
- Every silent-failure fix must **log** via `Msg("! [gltf] ...")` and either reject cleanly or substitute
  a safe default — never leave a path that produces garbage with no diagnostic.
- When you change behavior, update the matching row in `GLTF_SPEC_COVERAGE.md` so the docs stay honest.
- Keep the `external_*` shaders and their `compressor/mod/shaders/r3/` mirrors **in sync** (the build
  pulls from one; the compressor from the other).
- Prefer the existing patterns already in the file (e.g. the `ext_white_texture_name` 1×1 stand-in, the
  `$user$` no-disk texture binding, the per-material child split) over inventing new machinery.
- If a fix turns out larger or riskier than its phase implies, **stop and report** with what you found and
  the options — don't silently expand scope or push a half-done change.

## Test assets
Use the Khronos glTF-Sample-Assets where possible. Targeted repros named in the reviews:
- Sparse accessors / morph bases — any asset with `accessor.sparse` (build a minimal one if needed).
- `doubleSided` — foliage/cloth assets; verify back faces no longer vanish.
- Factor-only metals — a material with `metallicFactor=1` and **no** MR texture (should read as metal).
- `CesiumMan` — skinned root-bone offset (collision box) + bind pose.
- `BoxTextured` with `KHR_texture_transform`, `AlphaBlendModeTest`, `ABeautifulGame` (gloss),
  `BoxVertexColors`, an ORM-packed asset, and a `TEXCOORD_1`/lightmap asset.

---

## PHASE 1 — Kill silent corruption (do first; mostly small, OGF-safe)

**1.1 Sparse-accessor guard — HIGHEST PRIORITY.**
`cgltf_accessor_read_float/_uint/_index` return `0` *without writing the output* when `accessor->is_sparse`
(`cgltf.h:2357, 2501, 2521`), and the loader **drops that return at every call site**
(`FExternalVisual.cpp:741–750, 779–781, 1387–1395, 1427–1429`). Result: a sparse POSITION accessor
collapses every vertex to the origin, silently.
Fix (pick one, document which): either route attribute reads through `cgltf_accessor_unpack_floats`
(which *does* apply sparse — verified `cgltf.h:2420`) into a temp array, **or** detect
`accessor->is_sparse` up front and reject-with-log. Also start checking the bool return of the read
functions and bail+log on `false`. Correct the stale ✅ in `GLTF_SPEC_COVERAGE.md:28`.

**1.2 `extensionsRequired` front-door gate.**
Parsed by cgltf but never read. After each `cgltf_parse` (`~:629, :449, :1309`), walk
`gltf->extensions_required[]` against a small allowlist (`KHR_materials_emissive_strength`,
`KHR_texture_transform`, `KHR_mesh_quantization`, plus anything you add this pass). Unknown →
`Msg("! [gltf] requires unsupported ext '%s'")` + `return false`. One gate protects Draco/Basis/meshopt at
once.

**1.3 Draco / meshopt detect-and-reject.**
cgltf parses the metadata but does not decompress. Guard `primitive.has_draco_mesh_compression` and
`buffer_view.has_meshopt_compression`; reject+log. (Covered for the *required* case by 1.2, but guard
explicitly too, since these can appear without being in `extensionsRequired`.)

**1.4 Vertex-index sanity (hardening, finding G).**
The triangle loop is overrun-safe but never validates that an index is `< vertex_count`. Add a bounds
check; on out-of-range, log once and skip the triangle (don't feed a bad index to the VB).

---

## PHASE 2 — Trivial correctness wins (LOW effort, shader/loader only)

**2.1 MASK `baseColorFactor.a` (audit 2.6).** `ext_base_color.w` is hardcoded `1.f`
(`FExternalVisual.cpp:1565`, skinned `:1248`). Push the captured `base_alpha` into `.w`, and in
`deffer_base_ext_flat.ps` do `base.a *= ext_base_color.w;` **before** the `clip( base.a - ext_alpha_cutoff.x )`.

**2.2 Vertex colors on the textured paths (audit Tier 3).** `COLOR_0` is multiplied into albedo only on
the no-texture (`external_vc`) path. Multiply it in the textured/bump PSes too — the element is already in
the vertex decl.

**2.3 `KHR_texture_transform` rotation + per-map decoupling (finding C).** Currently offset+scale only, and
the base-color texture's transform is force-applied to *all* maps. Add sin/cos rotation to `ext_uv()` in
`external_common.h`. If feasible without per-map constant bloat, stop applying base-color's transform to
maps that don't declare one.

**2.4 AO on non-MR shaders (audit Tier 3).** Wire `s_ao` + the lerp into `_flat.ps`/`_bump.ps` for the
AO-without-MR case.

---

## PHASE 3 — High-value visible fixes (MED effort, stock-safe)

**3.1 Factor-only metals (audit 2.2).** Shader selection keys on MR *texture* presence
(`FExternalVisual.cpp:1192`, gate `:418`), so `metallicFactor`-only metals fall to the flat shader. When
`has_pbr_metallic_roughness` is set but there's no MR texture, bind a 1×1 **white MR** stand-in (mirror
`ext_white_texture_name`) and select `external_mr`. Closes factor-only metalness in one change.

**3.2 `doubleSided` (audit 2.1).** Read `material->double_sided`; add a no-cull `.s` variant and flip the
normal on back faces via `SV_IsFrontFace` in the PS. Our `.s`/PS only.

**3.3 Default-scene recursion (audit 2.4).** The loader walks the flat `gltf->nodes[]`
(`:672, :1333, :393`). Recurse the default scene (`gltf->scene`, else `scenes[0]`), falling back to the
flat loop only if `scenes_count == 0`. Same `cgltf_node_transform_world`, correct *set* of nodes.

**3.4 Single model-wide auto-scale (audit 2.5, review 2B).** The `[0.5,2.0]`-unit shrink/grow runs
**per child** (`:894–921`), so multi-material models tear. Compute ONE factor in `FExternalKinematics`
and pass it to every child; make the band a fallback for absurd sizes only (e.g. >50u / <0.01u), honoring
real meters by default.

**3.5 Per-map `texCoord` routing (audit 2.8, finding B).** Only `at.index == 0` is captured
(`:716, :1361`) and no texture-view `.texcoord` is ever read, so every map samples UV0. Add a 2nd UV
channel to `vertExternal` + decl + shaders and route each map by its `texCoord`. Cross-cuts geometry +
shaders — do it as its own commit.

---

## PHASE 4 — Robustness

**4.1 MikkTSpace tangents (audit 1.4).** Absent TANGENT + a normalTexture currently yields constant
`T=(1,0,0)` (`:736, :762`) → mis-lit bumps. Vendor `MikkTSpace.c` (public domain) or a per-triangle
UV-gradient fallback; generate **before** the node-world bake + Z-flip, only when a normal map is present.

**4.2 Samplers / wrap modes (audit 2.7).** Add named sampler states (clamp/mirror) and select per-material
from `cgltf_sampler->wrap_s/_t`. Filter modes lowest priority.

**4.3 Skinned MASK (finding I).** `ext_alpha_cutoff` is hardcoded `-1` on the skinned path (`:1250`). Wire
the real cutoff through so skinned alpha-cutout works.

**4.4 First-skin / first-material handling (findings E, F).** `gltf->skins[0]` is hardcoded (`:471`) and
the merge-all path latches material 0's maps (`:791–861`). At minimum **log** when a model has >1 skin or
when merge-mode discards extra materials, so these stop being invisible.

**4.5 Skinned collision box on non-identity root (audit 2.3).** Carry the box on a guaranteed-identity
synthetic root (extend the multi-root synth logic to all skinned models), or express it in
`inverse(rootBoneWorld)` space. Verify against CesiumMan.

---

## PHASE 5 — Animation playback (the pillar — separate, multi-day; only if Phases 1–4 are solid)

Bind pose only today (no `cgltf_animation` code exists). Design is already worked out in
`GLTF_CONFORMANCE_AUDIT.md` Tier 4 and the skinning research:
- Parse `cgltf_animation` channels → map `target_node` to our bone via the captured `ExtBone::gltf_node`.
- Sample TRS at playback (binary-search keyframes; **LINEAR with quaternion slerp + STEP first**,
  CUBICSPLINE next); compose local TRS, conjugate by `C`, drive the bones, then `CalculateBones`.
- **CRITICAL:** `bind_transform` is on the **shared** `CBoneData` — writing animated locals there makes
  ALL instances share one pose. Per-instance animation **must** go through `CBoneInstance` callbacks
  (`bi.set_callback`, `callback_overwrite()` at `SkeletonCustom.cpp:167–176`). Design around bone-instance
  callbacks from the start.

---

## Do NOT attempt (architecturally blocked — see `GLTF_REVISIT.md`)
True dielectric F0/Fresnel, tinted SSR, per-pixel metal flag in the FP16 G-buffer, sRGB-correct
baseColor/emissive (engine is a gamma pipeline), HDR emissive bloom, and putting BLEND surfaces in SSR.
These require shared-renderer surgery that would regress stock content. Leave them.

## Definition of done (per phase)
1. Builds clean (R3 and R4 render paths).
2. The targeted test asset(s) render/behave correctly **and** a stock OGF asset is verified visually
   unchanged in the same scene.
3. **Parity gate (shared files).** If the change touches *any* shared file — anything that is **not**
   `FExternal*.{cpp,h}` and **not** `gamedata/shaders/r3/external_*.{s,ps,vs,h}` (e.g. `SkeletonCustom.cpp`,
   `ModelPool.cpp/.h`, `Fmesh.h`, the *ResourceManager* files) — it must ship with a fresh **stock-scene
   RenderDoc A/B** (baseline vs HEAD) showing **no change on any stock draw**: identical draw set/order,
   identical per-draw pipeline state (blend/depth/stencil ref+mask+op/cull/RT/shader bytecode), and a clean
   framebuffer pixel diff. Procedure + exact `rdc` commands: see *Empirical verification — RenderDoc A/B*
   in `GLTF_OGF_PARITY_ANALYSIS.md`. A trivially-provable behavior-identical edit (e.g. caching a value) may
   record the static argument in lieu of running the capture, but must say so explicitly. Any stock-draw
   difference = regression → stop and revert/bisect, do not ship.
4. No new silent-failure paths — every rejection logs.
5. **A verification subagent that did not write the code has re-reviewed the diff** against the spec, the
   review-doc claim, and the hard rule, and signed off.
6. `GLTF_SPEC_COVERAGE.md` rows updated to match new reality.
7. Committed with a descriptive message.

Start with Phase 1.1 (sparse) — it's the highest silent-corruption risk and the rest build on a trustworthy
loader. Kick it off by fanning out the recon subagents on the Phase 1 call sites, then implement, then
gate on the verification pass before Phase 2.
