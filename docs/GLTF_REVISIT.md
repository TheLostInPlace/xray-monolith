# glTF External Models — Revisit / Parking Lot

Things we've intentionally **parked** while building out glTF/GLB support — deferred tradeoffs,
engine limitations, and roadmap items. The goal is so we can come back to any of these later without
re-deriving the context. (Companion to `docs/GLTF_GLB_Integration_Research.md`.)

Hard rule for everything here: **nothing may break how OGF/OMF or stock content renders.** Several
items are parked *specifically because* the clean fix would touch X-Ray's shared lighting/combine.

Last updated: 2026-06-23.

---

## A. Parked tradeoffs — revisit if a model looks wrong

### A1. Glossy *dielectric* surfaces look too "chrome" / over-reflect the sky
- **Seen on:** ABeautifulGame (chess pieces & board), any smooth non-metal.
- **Root cause (inferred):** X-Ray's gloss-driven reflection (cubemap + SSR) has **no dielectric Fresnel
  cap** — any high-gloss surface reflects at near-mirror strength, regardless of metalness. (This is
  inferred from the single gloss channel coupling reflection-strength + highlight and the shared-combine
  structure, not read line-by-line in the un-shipped combine.) A real PBR viewer
  gives a glossy dielectric only ~4% reflection (F0=0.04), rising at grazing angles. Compounded by our
  world reflecting the **bright open sky** vs the soft studio HDRI in Khronos reference renders.
- **Verified NOT our metalness:** those pieces are metallic≈0.06 (dielectric); our colored-reflection
  overlay correctly contributes nothing. This is shared-engine behavior (affects stock glossy too).
- **Stock-safe lever (not done):** make our roughness→gloss curve less aggressive for dielectrics, e.g.
  `gloss = (1-roughness)^2` in the deferred MR PS. Tradeoff: X-Ray couples reflection strength AND
  highlight sharpness into the single `gloss` value, so this also softens the specular highlight.
- **Proper fix (blocked):** apply dielectric F0 + Fresnel in the shared combine — same stock-safety wall.

### A2. Metalness final model — pinned, working
- Smooth metal = mirror (no diffuse, engine reflection + colored overlay tint); rough metal = stable
  matte diffuse; gloss stays plain `1-roughness`. See `memory/gltf-metalness-wanted.md` for the full
  tuning history (3 rounds: diffuse-swing fix, roughness-aware overlay, the "smooth metal went black"
  revert). Revisit only if a new asset misbehaves.

### A3. Metal reflection tint is subtle
- The (untinted) engine reflection dominates smooth metal; our overlay only adds the color tint. Could
  boost the overlay if a more saturated metal color is wanted. Low priority.

---

## B. Engine limitations — would need shared-shader surgery (stock-safety wall)

### B1. True HDR emissive bloom / unclamped emissive_strength
- Emissive is a correct **LDR additive** overlay (respects emissiveFactor × emissive_strength) but does
  NOT bloom and clamps at 1.0. True HDR bloom needs hooking `phase_combine` itself, OR the engine's
  `dx11_hdr10` mode (then it'd bloom for free — the emissive already renders pre-bloom into rt_Generic_0).

### B2. Tinted screen-space reflections for metals
- Metals get colored *cubemap* reflection (our overlay) but the engine's sharp **SSR is untinted**.
  Tinting SSR needs the shared combine. Out of reach stock-safely.

### B3. Per-pixel metal flag in the G-buffer — RULED OUT, do not re-attempt
- Position RT is **FP16** — *verified* (`D3DFMT_A16B16G16R16F` in `r3/r4/r2_rendertarget.cpp`) — and the
  hemi/mtl float is bit-packed to the limit; no free bit survives the FP32→FP16 round-trip.
- The "5-bit material-ID value wraps (`&31`) and stock sweeps all 31 slots, so no reservable slot either"
  sub-claim is **inferred, not line-verified**: it rests on the base shader-pack `common.h`, which is not
  shipped in this repo and so has not been read here. The FP16 no-free-bit conclusion stands on its own
  regardless. This is *why* metalness lives entirely in our own passes.

### B4. Transparent (BLEND) surfaces don't appear in screen-space reflections
- A BLEND surface renders **forward, after the deferred combine** — and SSR is computed *in* that combine,
  so the transparent panel doesn't exist in any buffer when SSR runs. To appear in SSR a surface must be
  **opaque and in the G-buffer pre-combine** — the opposite of transparent. This is fundamental to ALL
  deferred renderers; **stock X-Ray glass has the same trait** (glass isn't in SSR either). Writing depth
  in the blend pass can't help (it's too late in the frame).
- Note: the janky reflections noticed on AlphaBlendModeTest (a panel reflected where it couldn't be, or a
  reflection where there's nothing) were the **mod's SSR being approximate** (screen-space: reflects
  camera-facing pixels, smears off-screen) — not our rendering; it hits stock surfaces too.
- Parked lever (not done, not default): render *near-opaque* BLEND materials as **MASK** so they enter the
  G-buffer + SSR, at the cost of true see-through (hard alpha-test edges). Genuinely-transparent things
  (glass) inherently can't be in SSR. **Recommendation: accept** (standard behavior).
- BLEND also has a safety highlight roll-off (`lit/(1+max(lit-0.9,0))`) so its forward lighting can't
  hard-clamp to flat white in bright scenes; ~identity in the normal range.

### B5. Shadow / dynamic-light parity with stock OGF — DONE (robust pass 2026-06-23, pending tune)
- **How the engine casts shadows:** `rimp_select_sh_static/dynamic` (r4.cpp) returns `E[SE_R2_SHADOW]`
  = **E[2]** for EVERY shadow phase (sun cascade, point, spot). The `.s` compiler maps `l_point`→E[2],
  `l_spot`→E[3], `l_special`→E[4] with **no fallback**. So shadows must fill **E[2]** (l_point); our old
  `l_special`-only setup filled E[4], the wrong element.
- **The fix:** every external lit `.s` now defines `l_point` (+ `l_spot`/`l_special` aliased to it) so
  E[2]/E[3]/E[4] are all filled, with the EXACT stock caster setup — `dumb` PS, **default cull**,
  ztest+zwrite, colour off (the old back-face cull was dropped; it broke thin geometry). The VS is our
  own **`shadow_ext_model.vs`** = stock `shadow_direct_model.vs` + a **normal-offset depth bias**
  (`EXT_SHADOW_NORMAL_OFFSET`, currently 0.035 model-units): pushes the caster along its normal so
  neither smooth surfaces self-shadow (acne — why we'd used back-face) nor thin geometry self-shadows
  under a close light. Tune that one constant: too small → acne/self-shadow; too large → peter-panning.
- **Stock-safe:** only our `.s` + our new VS; stock OGF keeps `shadow_direct_model` + the C++ blender.
- Pairs with the **metal diffuse-floor** (`EXT_METAL_DIFFUSE_KILL` in external_common.h, 0.6 → keep 40%
  diffuse) so metals are visible under a flashlight at night instead of pure-PBR black.

---

## C. Not yet implemented — roadmap (rough priority)

Material pipeline is essentially complete: albedo, normal, metallic-roughness→gloss, **real metalness**,
multi-material, emissive (factor×strength), **alpha OPAQUE/MASK/BLEND**, **occlusion (AO)**. Remaining:

1. **Animation / skinning** — the big one and the natural next pillar (static bind pose only today). Two
   tiers: (a) *rigid node animation* (TRS keyframes on nodes — rotating/bouncing props), moderate; (b)
   *skinning* (JOINTS_0/WEIGHTS_0 + bone hierarchy + morph targets; CMotionDef/CPartition/CBlend
   transcode), hard. (a) is the sensible first step.
2. **Quick correctness wins** — vertex colors (COLOR_0), KHR_texture_transform (UV offset/rot/scale),
   material factors (baseColorFactor, metallic/roughnessFactor, normalScale). Cheap, improve fidelity.
3. **Smaller material gaps** — AO on bump-only/static shaders (currently only the MR shaders get AO);
   factor-only metals (need a constant + white MR stand-in); roughness-based reflection blur (no mip
   chain on the env cubes); BLEND lighting polish (point lights/shadows on transparent surfaces).
4. **Advanced KHR materials** — clearcoat, transmission, volume, ior, specular, sheen, anisotropy,
   iridescence, dispersion, unlit, variants. Long tail; most engines partial-support these.
5. **Compression / encoding** — Draco mesh, KHR_mesh_quantization, KHR_texture_basisu (KTX2). A
   Draco/Basis asset currently won't load.
6. **base64 `data:` URI images** — embedded bufferView + external-file images work; base64 data URIs
   are not decoded (rare in practice). See D2.
7. **Second UV set** (TEXCOORD_1), **sparse accessors**, **texture samplers** (wrap/filter modes).

---

## D. Resolved — do NOT re-investigate

### D1. sRGB color space — leave albedo/emissive as UNORM
- X-Ray runs a **gamma pipeline**: stock textures are legacy DXT1/DXT5 (non-sRGB UNORM) and no stock
  shader linearizes albedo. Our UNORM decode already matches. Forcing "spec-correct" sRGB would
  linearize only *our* albedo → our models darker than every stock surface. (The old memory note that
  said "force R8G8B8A8_UNORM_SRGB" was wrong for this engine.)

### D2. Embedded-texture decoding — DONE
- `ext_decode_gltf_image` (FExternalVisual.cpp) decodes via D3DX11 from memory:
  (1) **GLB bufferView images** (the main embedded case — every test GLB uses this) and
  (2) **external image files** (URI, resolved relative to the glTF, percent-decoded).
  Only **base64 `data:` URIs** are not handled (tracked in C9).
