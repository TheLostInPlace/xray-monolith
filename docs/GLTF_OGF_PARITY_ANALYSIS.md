# Independent Analysis — OGF/OMF Parity & "Stock-Safe" Claims

**Date:** 2026-06-23. **Author:** independent review, separate from the sessions that wrote
`GLTF_CONFORMANCE_AUDIT.md`, `GLTF_SPEC_COVERAGE.md`, and `GLTF_REVISIT.md`.

**Question under review.** The glTF/GLB work rests on a single load-bearing promise, repeated throughout the
docs and used to justify the entire "parked / architecturally blocked" list: *nothing it does changes how
stock OGF (static) or OMF (animated) content loads, renders, animates, or collides.* This analysis tests
that promise against the actual source — both the **isolation claim** (is the glTF path really separable
from stock?) and the **blocked-item justifications** (are the "this would break stock" reasons technically
true, or over-conservative?).

**Method.** Three independent verification passes over the real code: (1) every shared C++ file the glTF
work modified, via `git show`; (2) the external shaders and their interaction with shared shader state; and
(3) the specific engine-internals claims (FP16 G-buffer, shadow element routing, the deferred combine,
the gamma pipeline, emissive bloom). Key facts were then re-confirmed by hand. No files were modified.
This is **static analysis** — the engine was not built or run (see *Confidence & limits*).

---

## Verdict in one paragraph

The parity promise is **substantively true, but the wording "OGF/OMF byte-for-byte untouched" is
imprecise and should be softened.** The glTF work *does* modify shared engine files — `ModelPool.cpp/.h`,
`Fmesh.h`, `SkeletonCustom.cpp`, two `*ResourceManager_Scripting.cpp`, and it adds pixel shaders that
`#include` stock `common.h`. Every one of those changes is **gated or purely additive**, so stock OGF/OMF
behavior is preserved — but it is *behavior-identical*, not *instruction-identical*. There are two
cosmetic, non-behavioral exceptions (a redundant cast on two cold paths; an extra string compare on the
model-load path). On the blocked list, the engine-limitation justifications are **technically sound** —
two are airtight, two are confirmed, and one is half-confirmable in this repo. The parking decisions are
conservative in the *defensible* sense (protecting the deferred + gamma + LDR pipeline and stock parity),
not over-broad. The single documentation fix I'd insist on: stop calling it "byte-for-byte untouched," and
flag the one blocked sub-claim that depends on shader code not shipped in this tree.

---

## Part 1 — Is the glTF path actually isolated from stock?

### 1.1 The honest framing: shared files *were* modified
"OGF byte-for-byte untouched" is true of the **OGF/OMF data formats and their runtime behavior**, and it
is the right *intent*, but as a literal statement about the codebase it is over-stated. The glTF work
touched these shared (non-`FExternal*`, non-`cgltf`) files:

| File | Change | Stock impact |
|---|---|---|
| `src/xrEngine/r2_types.h`→`Fmesh.h` enum | Appended `MT_EXTERNAL_STATIC=13`, `MT_EXTERNAL_SKINNED=14` | None — an OGF `u8` type field only ever holds stock values 0–12; the new values can't be produced by reading a stock file. |
| `ModelPool.cpp/.h` | New `case MT_EXTERNAL_*` **before** the preserved `default: FATAL`; new `is_external_format()` early-return in `Instance_Load`; cache-key guard | Stock path provably identical (see 1.2). |
| `SkeletonCustom.cpp` | 6 null-guards around `LL_GetChild`/`smart_cast<CSkeletonX*>` | Behavior-identical for stock (see 1.3); one cosmetic caveat. |
| `ResourceManager_Scripting.cpp` + `dx10ResourceManager_Scripting.cpp` | `.s` element functor now passed 2 extra texture args (`t_2, t_3`) | Stock `.s` declare 4 params; Lua discards extras → identical `ShaderElement`. |
| `deffer_base_ext_*.ps` (new files) | `#include "common.h"` read-only | No stock shader/header modified (git-verified). |

`r__dsgraph_build.cpp` and `SkeletonRigid.cpp` were **not** modified by the glTF work (they appeared in a
keyword grep but no glTF commit touches them).

**So the accurate claim is:** "stock OGF/OMF *behavior* is unchanged; the integration is additive and
gated" — not "no shared code was touched."

### 1.2 Load / dispatch / cache — SAFE
- **Dispatch:** the new enum cases sit ahead of the original `default: FATAL("Unknown visual type")`,
  which is preserved. Stock types never reach the new cases; no stock `switch` acquired an unhandled case
  (checked the type switches in `ModelPool.cpp` and `r__dsgraph_build.cpp` — all retain a stock-routing
  `default`).
- **Cache key:** the one subtle change — `if (char* _ext = strext(low_name)) if (!is_external_format(low_name)) *_ext = 0;` — provably no-ops for stock: `is_external_format` is false for any `.ogf`/extensionless name, so the extension is stripped exactly as before, yielding the identical cache key. Only `.gltf`/`.glb` keep their extension (to get a distinct key). **Cost to stock:** one extra `strext`+compare per `Create/CreateChild/Exists` call — pure CPU, no behavior change.
- **Spawned external models** report `Type = MT_SKELETON_RIGID` at runtime, so they ride the *existing*
  rigid-skeleton render path; the child `FExternalVisual` is drawn via the **virtual** `Render()` in
  `ModelPool::Render`, never re-dispatched through a type switch. Even an `MT_EXTERNAL_STATIC` reaching
  `add_Static` would hit the stock-routing `default` (`r_dsgraph_insert_static`) — same render buckets,
  no new sort/state for stock draws.

### 1.3 Skeleton / per-frame hot paths — SAFE (one cosmetic caveat)
- `LL_GetChild` itself was **not** changed; it already did the `fast_dynamic_cast<CSkeletonX*>`. For a
  pure stock OMF every child *is* a `CSkeletonX`, so every new guard (`if (c)`, `if(!_c) continue;`)
  evaluates exactly as the old code did. Behavior preserved.
- The render-critical paths — `CalculateBones`, `Bone_Calculate`, the per-frame skeleton render, and the
  stock `Load`/`AfterLoad` child loop — were **not touched**. The guards add zero per-bone / per-frame
  work to stock skeletons.
- **Cosmetic caveat (worth a one-line cleanup):** in `PickBone` and `AddWallmark` the guard is written
  `if (LL_GetChild(i) && LL_GetChild(i)->…)`, so a `fast_dynamic_cast` now runs **twice** per child on
  those two **cold** paths (ray-pick, wallmark application). Identical result, marginally more work — this
  is the literal sense in which "byte-for-byte" is over-stated. Fix: cache `if (CSkeletonX* c = LL_GetChild(i))`.

### 1.4 Shader state isolation — SAFE
- **No stock shader file or shared header was modified** (`git log` on `common.h`, `lighting.h`,
  `deffer_base.ps`, etc. is empty for the glTF work). The new `deffer_base_ext_*.ps` *consume* `common.h`
  read-only; `external_common.h` only defines new `ext_*` symbols under an include guard.
- **Constant registers can't bleed.** `RCache.set_c(name, …)` resolves the name against the *currently
  bound shader's* reflection table and is a **no-op if the name is absent**. The `ext_*` constants are
  written only while an external shader is bound and exist only in that shader's table; a subsequent stock
  draw rebinds to the stock table where `ext_*` simply don't exist. There is no shared fixed-register
  aliasing.
- **No manual render state in the glTF C++.** `FExternalVisual::Render` performs only `set_c`,
  `set_Geometry`, an optional 32-bit IB rebind, and `RCache.Render` — no `set_RT`, `set_Blend`,
  `set_Stencil`, `set_Sampler`, or `GenerateMips`. All blend/depth/stencil/sampler state lives in the
  `.s` blenders and is applied/restored per-Element by the standard backend, exactly as for stock draws.
- **Stencil mark is identical to stock.** External opaque geometry writes `dx10stencil_ref(1)` (ref
  `0x01`, write-mask `0x7f`, REPLACE) — the **same deferred mark** stock geometry writes
  (`Blender_Model_EbB.cpp:250`, `Blender_BmmD.cpp:291`, etc. all `r_StencilRef(0x01)`). The shared combine
  therefore treats external pixels exactly like stock geometry; stock pixels' stencil is never touched.
- Forward overlays (emissive/metal/blend, `:sorting(2,true)`) composite additively into the engine's
  existing Sorted-forward target — the same path stock transparents/particles use — and read engine
  globals (`env_color`, `s_env0/1`, `L_sun_*`) **read-only**. Nothing stock-facing is written.

**Net for Part 1:** the isolation holds. The only honest corrections are terminological ("behavior-
identical, additive, gated" ≠ "byte-for-byte untouched") plus the redundant-cast cleanup.

---

## Part 2 — Are the "would break stock" blocked-item justifications true?

These are the claims in `GLTF_REVISIT.md` B1–B5 and the audit's "Architecturally BLOCKED" table.

### B5 — Shadows must fill element E[2] (`l_point`) — **TRUE (airtight)**
`r4.cpp:73,84` set `int id = SE_R2_SHADOW;` and return `pVisual->shader->E[id]` for every shadow phase.
`SE_R2_SHADOW = 2` is the **active** define (`r2_types.h:144`, R2/R3/R4), and the enum confirms element 2 =
`SE_R1_LPOINT` (`Shader.h:198`). The DX10/11 `.s` compiler maps `l_point→E[2]`, `l_spot→E[3]`,
`l_special→E[4]` as independent `if`s with **no fallback** (`dx10ResourceManager_Scripting.cpp:584–596`).
A shader defining only `l_special` leaves E[2] null and casts no shadow. The doc's fix (define `l_point`,
alias the rest, in the mod's *own* `.s`) is correct and genuinely stock-safe. **Verified line-by-line.**

### B1 — HDR emissive bloom needs the shared combine / `dx11_hdr10` — **TRUE (confirmed)**
The emissive overlay is forward + additive (`one,one`), drawn **after** the deferred combine, into
`rt_Generic_0` — which is `D3DFMT_A8R8G8B8`, an **8-bit LDR** target. So additive emissive clamps at 1.0
before bloom samples the generic buffer. Driving bloom above 1.0 would require routing emissive into an
HDR (FP16) pre-tonemap target or the `dx11_hdr10` path — i.e. shared-pipeline surgery, exactly as stated.

### D1 — Gamma pipeline; leave glTF albedo UNORM — **TRUE (confirmed)**
No `_SRGB` texture format is ever selected: every active base-color map in `dx10TextureUtils.cpp` is
`_UNORM` (the `_SRGB` tokens are comments), and the main DDS path uses the header format with no SRGB
filter flag (`MakeSRGB`/`D3DX11_FILTER_SRGB`/`FORCE_SRGB` are absent repo-wide). The shipped combine does
tonemap + gamma but **no albedo linearization**. Forcing sRGB on glTF albedo would darken only glTF
surfaces relative to stock — the UNORM decision is the correct *consistency* call for this engine.

### B2 / A1 — Dielectric Fresnel & tinted SSR live in the shared combine — **TRUE (structural)**
Gloss is a **single scalar** packed into `albedo.a` (`pack_gbuffer(... float4(albedo, gloss))`); there is
no separate reflection-strength channel, so the one value that sharpens the highlight also drives
reflection. Reflection/SSR are accumulated in shared, non-per-material passes (`phase_combine`,
`s_accum_reflected`). A *proper* dielectric F0=0.04 Fresnel cap therefore does hit the shared-combine
wall. **Caveat:** the exact gloss→reflection formula lives in the base pack's `accum_reflected.ps`/
`combine.ps`, which are **not shipped in this repo**, so "no Fresnel cap exists" is inferred from the
single-channel G-buffer + shared-pass structure, not read directly. The doc's own mod-side lever
(`gloss = (1-rough)^2` in the external MR PS) is correctly identified as achievable without touching stock.

### B3 — FP16 position RT, no free bit / no reservable material slot — **HALF-CONFIRMED**
- **TRUE:** the position RT is `D3DFMT_A16B16G16R16F` (FP16) in R2/R3/R4
  (`r3_rendertarget.cpp:396`, `r4_rendertarget.cpp:490`, `r2_rendertarget.cpp:258`), and material is
  carried as a *continuous packed float* in `.w` (`r3.h:259`, `pack_gbuffer`). The "no free bit survives
  FP32→FP16" reasoning is sound on the format alone.
- **UNVERIFIABLE IN THIS REPO:** the specific "5-bit `&31` wrap, all 31 slots swept by stock" sub-claim
  lives in the base pack's `common.h` material-ID decode, which is **not shipped** here. It's plausible
  but unverified — and the conclusion (don't smuggle a metal flag through the G-buffer) is already
  well-supported by the FP16 finding regardless. **Doc fix:** mark this sub-claim as resting on
  un-shipped base-pack shader code rather than stating it as established fact.

### Other blocked rows — consistent
"Transparent surfaces can't be in SSR / G-buffer" is fundamental to deferred rendering and matches stock
glass behavior; "no mip chain on env cubes" for roughness blur is an engine-asset fact. Neither was
deeply re-derived here but both are standard and consistent with the verified pipeline structure.

**Net for Part 2:** the parking justifications are technically sound and conservative in the defensible
sense — they protect the deferred + gamma + LDR pipeline and stock parity. Two are airtight (B5, B1), two
confirmed (D1, B2/A1-structure), one half-confirmed (B3). None look like *over-conservative* excuses to
avoid work; where a stock-safe partial lever exists (A1 gloss curve, B5 shadow fix), the docs already say
so and route it through the mod's own shaders.

---

## Part 3 — Where the documentation should change

1. **Replace "OGF/OMF byte-for-byte untouched"** everywhere with "stock OGF/OMF *behavior* is unchanged;
   the glTF integration is additive and gated." It modifies shared files; the guarantee is behavioral,
   not literal-instruction.
2. **B3:** annotate the "5-bit `&31`, 31 slots swept" sub-claim as dependent on base-pack `common.h` not
   present in this repo (verified: FP16 position RT; unverified: the slot-exhaustion specifics).
3. **A1/B2:** keep the "blocked" label for the *proper* fix, but note the gloss→reflection formula itself
   was not read in-repo (base pack), so the absence of a Fresnel cap is inferred, not observed.

## Part 4 — One real (cosmetic) cleanup, not a parity bug

`SkeletonCustom.cpp` `PickBone`/`AddWallmark`: cache the child cast (`if (CSkeletonX* c = LL_GetChild(i))`)
to drop the double `fast_dynamic_cast` on stock ray-pick / wallmark paths. Behavior is already identical;
this just removes the only spot where stock instructions actually changed.

---

## Confidence & limits

- **Static analysis only.** No build, no runtime A/B. Dispatch-level, state-level, and shader-state-level
  parity are well-established by reading the code; *GPU-output* byte-for-byte parity on a stock scene is
  argued, not measured.
- **To prove it empirically:** load a stock-only level on the pre-glTF and post-glTF binaries and diff a
  RenderDoc capture of a stock-geometry frame (the repo already has RenderDoc tooling). If any stock draw's
  state/stencil/RT differs, it would show there. I expect no difference based on the above, but that
  capture is the difference between "argued safe" and "proven safe."
- **Base shader pack not in tree.** The B3 slot-exhaustion and the exact combine reflection math live in
  shaders this repo `#include`s but does not ship; those two points are correspondingly downgraded to
  inferred.

**Bottom line:** the stock-safety story holds. The glTF work is genuinely additive and behavior-preserving
for OGF/OMF, and the blocked-item justifications are real engine constraints, not avoidance. Tighten two
wordings, add the one-line cast cleanup, and (if you want certainty rather than a strong argument) close it
with a RenderDoc A/B on a stock scene.

---

# Empirical verification — RenderDoc A/B

> Status of the Part 3/4 items: **applied.** The three doc wordings (Part 3) are fixed; the Part 4
> `PickBone`/`AddWallmark` cast cleanup is in and the engine **builds clean** (DX11-AVX|x64, R3+R4,
> 0 errors). This section is the empirical close-out the "Confidence & limits" note asked for.

## Tooling — present and identified
- **RenderDoc** is installed (`C:\Program Files\RenderDoc`).
- **`rdc`** (the "Unix-friendly CLI for RenderDoc captures") is on PATH at
  `C:\Users\karam\.local\bin\rdc.exe`. It is purpose-built for exactly this A/B: `rdc capture` launches an
  app and grabs a frame; `rdc diff A.rdc B.rdc` compares two captures (`--draws`, `--pipeline MARKER`,
  `--framebuffer`, `--stats`, `--resources`); `rdc assert-state EID KEY --expect …` and
  `rdc assert-image A.png B.png` give machine-checkable gates; `rdc pipeline EID [blend|ps|vs|topology]`
  and `rdc rt` dump per-draw state / render targets. Run `rdc doctor` first to confirm the replay module is
  wired.

## C1 — the two builds
- **A (baseline / pre-glTF):** commit **`a511476f`** ("Vendor cgltf 1.15") — the parent of the first glTF
  *integration* commit `e16d686f` ("Add external GLTF/GLB static model loader (Phase 1)"). `a511476f` only
  vends an **unused** header, so its render behavior is the true stock baseline. (If a header-free tree is
  preferred, use `a511476f^`; behavior is identical.)
- **B (current HEAD):** this branch with the glTF work + the Part 3/4 fixes.
- Build A in an isolated worktree so HEAD is untouched:
  ```
  git worktree add ../parity-baseline a511476f
  pwsh -File ../parity-baseline/tools/pip-ship.ps1 -Build   # run from the worktree; emits its own _build exe
  ```
  Keep the two exes side by side, e.g. `A\AnomalyDX11AVX.exe` and `B\AnomalyDX11AVX.exe`.

## C2 — deterministic stock-only repro
The diff is only meaningful if both builds render the **same** frame. X-Ray has time-based motion (sun,
grass, water, particles, animation phase), so freeze everything:
1. A stock save whose view contains the parity-relevant stock draws: a **skinned stock NPC** (this is what
   exercises the changed `SkeletonCustom.cpp` render/pick/wallmark paths) **and** static OGF world geometry.
   No glTF asset in the scene.
2. From console: `g_dbg_draw_bounds 0`, identical resolution + render distance + all graphics options
   between runs, then freeze: `time_factor 0` and pause so the animation/particle phase stops advancing.
3. Fixed camera (don't move). Capture a settled frame the same way in both builds.

## C3 — capture the matched frame in each build
Inject without auto-grabbing, settle the scene, then trigger by hand so both captures are the same
game-state frame (wall-clock `--frame N` will **not** align because load times differ):
```
rdc capture --trigger --keep-alive -- "A\AnomalyDX11AVX.exe"     # inject; then load save, freeze, settle in-game
rdc capture-trigger                                              # queue a capture of the settled frame
rdc capture-list                                                 # note the CAPTURE_ID just produced
rdc capture-copy <CAPTURE_ID> A_stock.rdc                        # copy local (positional: CAPTURE_ID DEST, no -o)
# repeat identically against B\AnomalyDX11AVX.exe -> B_stock.rdc
```

## C4 — diff every stock draw
```
rdc diff A_stock.rdc B_stock.rdc --draws            # same draw set, same order
rdc diff A_stock.rdc B_stock.rdc --resources        # same RTs / shaders / buffers bound
for each stock draw EID:
  rdc diff A_stock.rdc B_stock.rdc --pipeline default --eid <EID>   # blend/depth/stencil(ref+mask+op)/cull/RT/shader bytecode
rdc diff A_stock.rdc B_stock.rdc --framebuffer --target 0 --threshold 0 --diff-output stock_diff.png
```
Optional machine gates: `rdc assert-state <EID> output.blend.0.enable --expect false`,
`rdc assert-image A_rt.png B_rt.png`, etc.

## C5 — PASS criteria
**PASS** = identical draw set/order (`--draws` empty) **and** identical per-draw pipeline state
(blend, depth, stencil ref+mask+op, cull, render targets, VS/PS bytecode) **and** a clean framebuffer pixel
diff (`stock_diff.png` empty / within threshold 0). Any difference on a **stock** draw = real regression →
stop and bisect.

## C6 — current status: **UNPROVEN UNTIL RUN**
The tooling is present and every command above is checked against the installed `rdc` surface, but the A/B
has **not been executed** here. The one step that cannot be made deterministic-and-trustworthy
non-interactively is C2/C3 — getting both builds to the *identical settled game-state frame* requires
loading a save, freezing time, and triggering the capture in a live game. Per the parity-task rule, that is
**not faked**: this is the runbook, not a result.

What *is* established without the capture:
- The only change touching a stock execution path is the Part 4 `PickBone`/`AddWallmark` edit, and it is
  **provably behavior-identical**: it caches the result of `LL_GetChild(i)` (called twice → once) and feeds
  the identical `c && c->PickBone(...)` short-circuit, with no change to control flow, iteration order,
  call arguments, or the values produced. The remaining shared-file deltas are comments only.
- The engine **builds clean** with the change (R3+R4).
So the static parity argument for *this* changeset is conclusive; the RenderDoc A/B is the standing gate for
*future* shared-file changes (see the "Parity gate" in `GLTF_FIX_PROMPT.md`), and the close-out measurement
whenever someone wants measured-rather-than-argued certainty.
