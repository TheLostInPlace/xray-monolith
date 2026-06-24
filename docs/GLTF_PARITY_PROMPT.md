# Hand-off Prompt — Prove & Harden OGF/OMF Parity

> Paste everything below the line into your Opus 4.8 session. Self-contained.
> Source of truth for this work: `docs/GLTF_OGF_PARITY_ANALYSIS.md` (independent parity review). Read it
> first. Companions: `GLTF_REVISIT.md`, `GLTF_CONFORMANCE_AUDIT.md`, `GLTF_SPEC_COVERAGE.md`,
> `GLTF_FIX_PROMPT.md`.

---

You are hardening and **proving** the central safety promise of the glTF/GLB loader: *stock OGF (static)
and OMF (animated) content loads, renders, animates, and collides exactly as it did before this work
existed.* An independent review (`docs/GLTF_OGF_PARITY_ANALYSIS.md`) found the promise **holds in code but
was only argued statically, never measured**, surfaced two documentation imprecisions, and one cosmetic
code cleanup. Your job is to (1) apply those concrete fixes, (2) **prove parity empirically** with a
RenderDoc A/B on a stock scene, and (3) turn that proof into a standing gate for all future glTF changes.

Read `GLTF_OGF_PARITY_ANALYSIS.md` end to end before doing anything — it contains the file/line evidence
for every item below.

## Hard rule (unchanged, and now the thing you're proving)
No change in this work may alter how stock OGF/OMF content renders, loads, animates, or collides. The
difference from before: you are no longer allowed to *assert* that — you must **demonstrate** it for any
shared-file change (see Task C).

## How to run this — orchestration
You are the lead. Same method as `GLTF_FIX_PROMPT.md`:
- **Recon in parallel** before editing — read-only subagents to confirm the exact call sites and the
  current doc wording.
- **A separate verification subagent gates each task** — it did not write the change; it re-checks against
  the analysis doc and the hard rule and tries to break it.
- **Small, reviewable commits** in the existing style (`glTF: …`, `docs: …`). Build between commits.
- **Stop and report** if anything is larger/riskier than scoped — especially if the RenderDoc diff shows
  *any* stock-draw difference (that would be a real regression, not a doc nit).

---

## Task A — Documentation corrections (LOW; do first, they're the cheap truth-fixes)

The analysis found three over-stated claims. Fix the wording wherever it appears (grep the `docs/` folder
and the in-code comments for each):

**A1. Drop "byte-for-byte untouched."** The glTF work *does* modify shared files (`ModelPool.cpp/.h`,
the `MT_EXTERNAL_*` enum in `Fmesh.h`/`r2_types.h`, `SkeletonCustom.cpp`, both
`*ResourceManager_Scripting.cpp`, and new PS shaders that `#include "common.h"`). Every change is gated or
additive, so stock *behavior* is preserved — but it is not literal-instruction-identical. Replace
"byte-for-byte untouched" / "OGF untouched" everywhere with: **"stock OGF/OMF behavior is unchanged; the
glTF integration is additive and gated."** Check `GLTF_CONFORMANCE_AUDIT.md`, `GLTF_SPEC_COVERAGE.md`,
`GLTF_REVISIT.md`, `GLTF_FIX_PROMPT.md`, and any source comments using that phrasing.

**A2. Mark the B3 sub-claim as inferred.** In `GLTF_REVISIT.md` B3 and the audit's BLOCKED table: the
**FP16 position RT is verified** (`r3_rendertarget.cpp:396`, `r4_rendertarget.cpp:490`,
`r2_rendertarget.cpp:258` — `D3DFMT_A16B16G16R16F`). But the **"5-bit `&31`, all 31 material slots swept
by stock"** sub-claim lives in the base shader pack's `common.h`, which is **not shipped in this repo**.
Annotate it as resting on un-shipped base-pack code (inferred), not established fact. The conclusion
(don't smuggle a metal flag through the G-buffer) stands on the FP16 finding alone.

**A3. Note the A1/B2 inference.** The gloss→reflection-strength formula and the absence of a dielectric
Fresnel cap live in the base pack's `accum_reflected.ps`/`combine.ps` (not in this tree). Keep the
"blocked" verdict for the proper fix, but mark "no Fresnel cap exists" as **inferred from the
single-channel gloss G-buffer + shared-combine structure**, not read line-by-line.

Verification subagent for Task A: confirm no remaining occurrence of "byte-for-byte"/"untouched" in a
sense that implies no shared code changed, and that B3/A1-B2 carry the inference caveat.

---

## Task B — The one real cleanup (TRIVIAL, behavior-identical)

`src/Layers/xrRender/SkeletonCustom.cpp`, in `PickBone` and `AddWallmark`: the guards are written
`if (LL_GetChild(i) && LL_GetChild(i)->…)`, so `LL_GetChild(i)` (a `fast_dynamic_cast`) runs **twice** per
child on the ray-pick and wallmark paths. Cache it:

```cpp
if (CSkeletonX* c = LL_GetChild(i))
    c->PickBone(...);   // or AddWallmark(...)
```

This is the *only* place stock instructions actually changed; the result is already identical, so this is
pure tidy-up. **Constraint:** it must stay behavior-identical — same children visited, same order, same
calls. Do not "improve" anything else in these functions.

Verification subagent for Task B: diff the two functions, confirm the visit set/order is unchanged and the
only delta is the eliminated redundant cast.

---

## Task C — PROVE stock parity with RenderDoc (the main event)

Static analysis says stock is safe; this task makes it measured. You have RenderDoc tooling available
(`rdc-tools`). The goal: show that, on a **stock-only scene**, every stock draw's pipeline state and the
final stock frame are unchanged between a pre-glTF baseline and the current build.

**C1. Establish the two builds to compare.**
- **Baseline:** the last commit before the glTF work began (find it: `git log --oneline` and pick the
  parent of the first `external`/`glTF` commit), built clean. If a pre-glTF binary already exists, use it.
- **Current:** HEAD, built clean (R3 and R4 paths).

**C2. Pick a deterministic stock-only repro.** A saved game or a level load that contains **only stock
OGF/OMF content** (no glTF models spawned), at a fixed camera position/time-of-day, so the same frame is
reproducible across both builds. Document the exact repro (save file, position, settings) so it can be
re-run.

**C3. Capture the same frame on both builds** with RenderDoc and pull them in via `rdc-tools`.

**C4. Diff what matters — per stock draw:**
- **Pipeline state:** blend, depth, **stencil (ref/mask/op)**, rasterizer (cull), bound render targets,
  and the bound shader bytecode for each stock geometry draw. These must be identical. (The analysis
  predicts they are — external geometry writes the same `StencilRef 0x01` deferred mark and the glTF C++
  performs zero manual state changes — so any difference here is a real regression to chase.)
- **Output:** a pixel diff of the final stock frame (and of the G-buffer/position/normal/albedo targets if
  reachable). Expect zero difference within rounding.
- **Draw set:** same number and order of stock draws; no stock draw newly skipped or added.

**C5. Pass criteria.** PASS = identical per-draw state on all stock draws + a clean (zero, or
sub-rounding) pixel diff of the stock frame + identical stock draw set. Any non-trivial difference → STOP,
report the specific draw and delta, and treat it as a regression, not a doc issue.

**C6. Record the result** in a new short section of `GLTF_OGF_PARITY_ANALYSIS.md` ("Empirical verification
— RenderDoc A/B"): the repro used, the builds compared, what was diffed, and the outcome. This converts
the doc's final caveat ("argued, not measured") into a measured result.

If the RenderDoc A/B is not runnable in your environment (no GPU capture, can't build the baseline),
**do not fake it** — instead produce the exact runbook (build steps, repro, capture steps, the specific
rdc-tools queries to compare state/pixels, and the pass criteria) so it can be executed on a machine that
can, and say clearly that C remains unproven until then.

---

## Task D — Make parity-proof a standing gate (process, cheap)

Add a short "Parity gate" note to `GLTF_FIX_PROMPT.md`'s definition-of-done: **any change that touches a
shared (non-`FExternal*`, non-`external_*`) file must ship with a fresh stock-scene RenderDoc A/B (per
Task C) showing no change to stock draws.** glTF-only files (FExternal*, external_*.s/.ps, cgltf) don't
need it; shared-file edits do. This is the durable lesson from the analysis: the risk surface is the
shared files, so guard exactly those.

---

## Definition of done
1. Tasks A, B, D applied and committed; docs build/read cleanly.
2. Task C either **executed with a recorded PASS** in `GLTF_OGF_PARITY_ANALYSIS.md`, or delivered as a
   complete, runnable runbook with an explicit "unproven until run" note.
3. A verification subagent that did not make the change has signed off on each task against the analysis
   doc and the hard rule.
4. No stock-draw difference anywhere in the RenderDoc diff (or the difference is surfaced and stopped on,
   not papered over).

Start with Task A (truth-fixes), then B (cleanup), then C (the proof) — and if C surfaces any stock-draw
delta, that supersedes everything else: stop and report it.
