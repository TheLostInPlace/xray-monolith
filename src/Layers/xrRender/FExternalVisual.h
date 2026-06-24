// FExternalVisual.h : static external-format (GLTF/GLB) render visual.
//
// Phase 1 of the external-model feature (see docs/GLTF_GLB_Integration_Research.md).
// FExternalVisual is a drop-in sibling of Fvisual: a self-contained renderable mesh
// owning its own VB/IB. It is created and populated by
// CModelPool::Instance_Create_External (NOT through the OGF dxRender_Visual::Load
// path); stock OGF/OMF behavior is unchanged -- the glTF integration is additive and gated.
//
//////////////////////////////////////////////////////////////////////
#ifndef FExternalVisualH
#define FExternalVisualH
#pragma once

#include "fbasicvisual.h"
#include "FSkinned.h" // CSkeletonX_ST (base for the skinned external visual)

class FExternalVisual : public dxRender_Visual, public IRender_Mesh
{
public:
	// --- dxRender_Visual overrides -------------------------------------------------
	virtual void Render(float LOD);
	// OGF entry point. Never used for external visuals (the new loader bypasses it);
	// present only to satisfy the vtable. Asserts if ever called with OGF data.
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();

	// --- External loader entry point -----------------------------------------------
	// Parse the GLTF/GLB at OS path `full_path` (already resolved by the model pool)
	// and build GPU buffers, bounding volumes and the shader. `short_name` is the
	// logical visual name (extension preserved), used for shader/texture resolution
	// and diagnostics. `material_filter` selects which material's primitives to load:
	// -1 (default) merges every primitive (single-material models); >=0 loads only the
	// primitives assigned to that glTF material index (one child per material, see
	// FExternalKinematics); -2 loads only primitives with no material. `pass` selects which batch to
	// build: ext_lit = the deferred lit batch; ext_emissive = the forward additive emissive OVERLAY
	// (external_emissive); ext_metal = the forward additive colored-reflection OVERLAY (external_metal).
	// The overlay passes return false if the selected material lacks the map they need. Returns false
	// if the filter selects no geometry.
	enum EExtPass { ext_lit = 0, ext_emissive, ext_metal, ext_blend };
	bool LoadExternal(const char* short_name, const char* full_path, int material_filter = -1, EExtPass pass = ext_lit);

	// One enumerated glTF material: its index (-1 = the no-material group), whether it carries an
	// emissive map, whether it is metallic (MR map with metallic_factor > 0), and whether it is
	// transparent (alphaMode=BLEND). FExternalKinematics uses these to pick the per-material children:
	// BLEND -> a single forward blended child; otherwise a lit child + optional emissive/metal overlays.
	struct MatInfo { int index; bool emissive; bool metallic; bool blend; };

	// Lightweight parse: collect the distinct materials used by triangle primitives (first-seen
	// order). Used by FExternalKinematics to decide how many per-material child visuals to build.
	static bool GetMaterialIndices(const char* full_path, xr_vector<MatInfo>& out);

	// One bone of a parsed glTF skin, expressed in X-Ray skeleton terms. The bone array is kept in
	// glTF skin.joints order, so glTF JOINTS_0 indices map straight onto bone ids (no remap table
	// when building the skinned VB). World bind transforms are absolute (cgltf_node_transform_world),
	// so the array need NOT be topologically sorted -- X-Ray's Bone_Calculate recurses over children.
	struct ExtBone
	{
		shared_str name;        // joint node name (or synthesized "$bone_N$")
		int        parent;      // parent BONE index (into ExtSkinData::bones), -1 for the root
		int        gltf_node;   // glTF node index of this joint (for animation-channel mapping later)
		Fmatrix    bind_local;  // local bind: X-Ray convention world_i = parent_world * bind_local
		Fmatrix    inv_bind;    // glTF inverseBindMatrix (model->bone); == CalculateM2B result (cross-check)
	};

	// Parsed glTF skin: the bone list + the single root bone index. valid()==false for non-skinned
	// models (then the caller uses the static/rigid path). A synthetic identity root is appended when
	// the glTF skeleton has multiple roots (X-Ray requires exactly one iRoot).
	struct ExtSkinData
	{
		xr_vector<ExtBone> bones;
		int                root = -1;
		bool valid() const { return !bones.empty() && root >= 0; }
	};

	// Parse the first glTF skin into ExtSkinData (joints, hierarchy, local bind, inverse-bind).
	// Returns false if the model carries no skin. Buffers are resolved (inverseBindMatrices live in a
	// buffer accessor). See FExternalKinematics for how this becomes a multi-bone CKinematics.
	static bool GetSkinData(const char* full_path, ExtSkinData& out);

	FExternalVisual();
	virtual ~FExternalVisual();

	// Index buffer is 32-bit (R32_UINT) instead of the engine-default 16-bit. Set when the merged
	// mesh exceeds 65535 vertices. Render() rebinds the IB format accordingly (the backend's
	// set_Indices() hardcodes R16_UINT). Copied by Copy() for instancing.
	bool m_index32 = false;

	// Emissive overlay child only: m_emissive marks it, m_emissive_scale = glTF emissiveFactor *
	// emissive_strength (KHR_materials_emissive_strength). Render() pushes it as the shader constant
	// "ext_emissive_scale" so the emissive PS multiplies the map by it. Copied for instancing.
	bool m_emissive = false;
	Fvector m_emissive_scale = {1.f, 1.f, 1.f};

	// Metal-reflection overlay child only: forward additive colored env reflection (external_metal).
	// No per-object constant needed (env cubemaps + env_color are engine globals). Copied for instancing.
	bool m_metal = false;

	// glTF alphaMode for the LIT batch. MASK -> the material's alphaCutoff (deferred PS clips texels
	// below it); OPAQUE/BLEND -> -1 (sentinel: clip never fires). Pushed as the shader constant
	// "ext_alpha_cutoff" in Render(). Copied for instancing. (BLEND is treated as opaque for now.)
	float m_alpha_cutoff = -1.f;

	// glTF occlusion strength for the LIT batch, but ONLY when occlusion is packed in the R channel of
	// this material's metallic-roughness texture (ORM). 0 otherwise (no AO / separate AO texture not yet
	// supported). Pushed as "ext_ao_strength"; the MR deferred PS modulates hemi by it. Copied.
	float m_ao_strength = 0.f;

	// Forward BLEND child only (glTF alphaMode=BLEND): m_blend marks it; m_blend_alpha = baseColorFactor.a
	// (overall opacity multiplier), pushed as "ext_blend_alpha" so the forward blend PS scales opacity.
	// This child renders forward/alpha-blended INSTEAD of writing the deferred G-buffer. Copied.
	bool m_blend = false;
	float m_blend_alpha = 1.f;

	// Shared per-material factors / texture transform (set for every lit/metal/blend child via set_c;
	// shaders include external_common.h and apply them). Defaults are identity so untouched materials
	// render unchanged. Copied for instancing.
	Fvector  m_base_color = {1.f, 1.f, 1.f};        // glTF baseColorFactor.rgb (albedo/F0 tint)
	Fvector  m_mr_factor  = {1.f, 1.f, 1.f};        // x=metallicFactor, y=roughnessFactor, z=normalScale
	Fvector4 m_uv_xform   = {1.f, 1.f, 0.f, 0.f};   // KHR_texture_transform: xy=scale, zw=offset
	Fvector2 m_uv_rot     = {1.f, 0.f};             // KHR_texture_transform rotation: x=cos, y=sin (identity)
	float    m_base_alpha = 1.f;                    // glTF baseColorFactor.a (MASK alpha multiplier)
};

// Skinned external (GLTF/GLB) render visual. A child of FExternalKinematics that deforms with the
// glTF skin via X-Ray's stock GPU skinning. It reuses CSkeletonX_ST whole -- the inherited Render()
// (-> CSkeletonX::_Render uploads the bone matrices to sbones_array and draws) and _Load_hw (packs
// our vertBoned4W source into the engine HW skinned vertex with correct normal quantization). We only
// add a glTF loader that builds the vertBoned4W stream (positions/normals/uv + JOINTS_0/WEIGHTS_0
// remapped to bone ids) and creates the shader with skinning mode 4 active (so the SAME external .s +
// stock model VS compile as the SKIN_4 variant -- skinning is VS-only, no new shader). OGF's
// CSkeletonX_ST is untouched. The matching skeleton is built by FExternalVisual::GetSkinData +
// FExternalKinematics; this class is the geometry half. Phase A renders bind pose (identity bone
// matrices); animation drives the bones in a later phase.
class FExternalSkinned : public CSkeletonX_ST
{
public:
	// OGF entry point -- never used (built via LoadExternal); guards the vtable.
	virtual void Load(const char* N, IReader* data, u32 dwFlags);

	// Sets the deferred-ext material constants (ext_base_color/ext_uv_transform/...) the PS needs, then
	// runs the inherited skinned render (sbones_array upload + draw). The base CSkeletonX_ST::Render does
	// NOT set those constants -- without this the shader reads stale/zero registers and the mesh goes
	// black/wrong. (FExternalVisual::Render does the equivalent for static children.)
	virtual void Render(float LOD);

	// Build the skinned render mesh from the glTF at `full_path`. `material_filter` matches
	// FExternalVisual (-1 merge / >=0 that material / -2 no-material). `skin` is the parsed skeleton
	// (bone ids that JOINTS_0 indexes). Returns false if the filter selects no skinned geometry.
	bool LoadExternal(const char* short_name, const char* full_path, int material_filter,
	                  const FExternalVisual::ExtSkinData& skin);

	// Bounding box of the mesh in its current (e.g. bind) pose -- each vertex skinned by Parent's bone
	// render-transforms. Used to set vis.box/physics from the RENDERED pose instead of the raw mesh
	// bbox (which is in a different space for Z-up-authored models). Requires Parent set + bones
	// calculated. No-op (returns invalid box) if either is missing.
	void CalcPoseBBox(Fbox& bb);

	// glTF baseColorFactor.rgb of this child's material (albedo tint); pushed as ext_base_color in
	// Render(). Default white = no tint. Copied for instancing via the inherited Copy (POD member).
	Fvector m_base_color = {1.f, 1.f, 1.f};
};

#endif // FExternalVisualH
