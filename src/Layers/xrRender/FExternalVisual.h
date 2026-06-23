// FExternalVisual.h : static external-format (GLTF/GLB) render visual.
//
// Phase 1 of the external-model feature (see docs/GLTF_GLB_Integration_Research.md).
// FExternalVisual is a drop-in sibling of Fvisual: a self-contained renderable mesh
// owning its own VB/IB. It is created and populated by
// CModelPool::Instance_Create_External (NOT through the OGF dxRender_Visual::Load
// path), so the OGF/OMF pipeline is completely untouched.
//
//////////////////////////////////////////////////////////////////////
#ifndef FExternalVisualH
#define FExternalVisualH
#pragma once

#include "fbasicvisual.h"

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
};

#endif // FExternalVisualH
