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
	// FExternalKinematics); -2 loads only primitives with no material. `emissive_pass` builds the
	// forward additive emissive OVERLAY (external_emissive) instead of the lit batch -- it returns
	// false if the selected material has no emissive map. Returns false if the filter selects no
	// geometry.
	bool LoadExternal(const char* short_name, const char* full_path, int material_filter = -1, bool emissive_pass = false);

	// One enumerated glTF material: its index (-1 = the no-material group) and whether it carries an
	// emissive map (so FExternalKinematics knows to add an emissive overlay child).
	struct MatInfo { int index; bool emissive; };

	// Lightweight parse: collect the distinct materials used by triangle primitives (first-seen
	// order). Used by FExternalKinematics to decide how many per-material child visuals to build.
	static bool GetMaterialIndices(const char* full_path, xr_vector<MatInfo>& out);

	FExternalVisual();
	virtual ~FExternalVisual();

	// Index buffer is 32-bit (R32_UINT) instead of the engine-default 16-bit. Set when the merged
	// mesh exceeds 65535 vertices. Render() rebinds the IB format accordingly (the backend's
	// set_Indices() hardcodes R16_UINT). Copied by Copy() for instancing.
	bool m_index32 = false;
};

#endif // FExternalVisualH
