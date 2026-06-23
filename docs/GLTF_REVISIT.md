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
- **Root cause:** X-Ray's gloss-driven reflection (cubemap + SSR) has **no dielectric Fresnel cap** —
  any high-gloss surface reflects at near-mirror strength, regardless of metalness. A real PBR viewer
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
- Position RT is **FP16** and the hemi/mtl float is bit-packed to the limit; no free bit survives the
  FP32→FP16 round-trip. The 5-bit material-ID value wraps (`&31`) and stock sweeps all 31 slots, so no
  reservable slot either. This is *why* metalness lives entirely in our own passes.

---

## C. Not yet implemented — roadmap (rough priority)

1. **BLEND (alphaMode=BLEND)** — the remaining alpha mode. Needs a forward-lit pass (deferred can't
   blend in the G-buffer). MASK (alpha-test) is DONE. *(next up)*
2. **AO on bump-only / static shaders** — AO currently only applies on the MR shaders (bump_mr, mr). A
   material with normal/albedo + a *separate* AO map but **no MR** wouldn't get AO. Rare.
3. **Factor-only metals** — metalness needs an MR *texture*; a material with only `metallicFactor` (no
   MR map) isn't treated as metal yet. Would need a constant + a white MR stand-in.
4. **Roughness-based reflection blur** — metal reflection picks sharp-vs-blurred by roughness via a
   normal-direction sample; no true mip-chain blur (the env cubes have no usable mips).
5. **Animation / skinning** — the big one (CMotionDef/CPartition/CBlend transcode, JOINTS/WEIGHTS,
   morph targets). Static bind pose only today.
6. **Vertex colors** (COLOR_0), **KHR_texture_transform** (UV offset/rot/scale), **material factors**
   (baseColorFactor, metallic/roughnessFactor, normalScale) — currently textures drive everything, scalar
   factors mostly ignored.
7. **Advanced KHR materials** — clearcoat, transmission, volume, ior, specular, sheen, anisotropy,
   iridescence, dispersion, unlit, variants. Long tail; most engines partial-support these.
8. **Compression / encoding** — Draco mesh, KHR_mesh_quantization, KHR_texture_basisu (KTX2). A
   Draco/Basis asset currently won't load.
9. **base64 `data:` URI images** — embedded bufferView + external-file images work; base64 data URIs
   are not decoded (rare in practice). See D2.
10. **Second UV set** (TEXCOORD_1), **sparse accessors**, **texture samplers** (wrap/filter modes).

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
