// FExternalVisual.cpp : static external-format (GLTF/GLB) render visual.
//
// Phase 1: load a static triangle mesh from a .gltf/.glb file and render it through
// the standard model shader pipeline. Mirrors Fvisual's buffer/geometry handling so
// it slots into the existing render graph, frustum culling and instancing with no
// changes to the OGF path. See docs/GLTF_GLB_Integration_Research.md.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(default:4995)

#include "../../xrEngine/fmesh.h"
#include "FExternalVisual.h"

#include "../xrRenderDX10/dx10BufferUtils.h"

#include "cgltf.h"

//////////////////////////////////////////////////////////////////////
// Configuration
//////////////////////////////////////////////////////////////////////

// glTF is right-handed (Y-up); X-Ray is left-handed (Y-up). Negating Z converts
// handedness and, as a mirror, flips triangle orientation so glTF's CCW front faces
// become the CW front faces DirectX expects -- hence no winding reversal is needed.
// Toggle to 0 if a particular exporter already authors left-handed data.
#define EXTERNAL_FLIP_Z 1

// Phase 1 shader (gamedata/shaders/r3/external_static.s). It reuses the stock deferred
// MODEL vertex/pixel shaders, which consume the exact D3DCOLOR-packed model vertex layout
// produced below, and renders the mesh fullbright (emissive) so it is visible regardless of
// scene lighting -- ideal for confirming geometry/UVs. Phase 2 swaps in a lit pbr_external.
#define EXTERNAL_DEFAULT_SHADER "external_static"

//////////////////////////////////////////////////////////////////////
// Vertex format
//
// Matches the engine's STATIC model vertex-shader input `v_model` exactly, verified against
// the deployed shaders (shaders/r3/common_iostructs.h struct v_model + deffer_model_flat.vs):
//   POSITION float3 (w defaults to 1), NORMAL/TANGENT/BINORMAL float3 = REAL [-1..+1] normals
//   (NOT D3DCOLOR-packed -- the SKIN_NONE path reads I.N directly, no unpack), TEXCOORD float2.
// We render as SKIN_NONE (skinning = -1, see LoadExternal + r4.cpp:1487). The flat model VS
// consumes P, N, tc; T/B are supplied so the layout is also valid for the bump model VS.
//////////////////////////////////////////////////////////////////////

static D3DVERTEXELEMENT9 dwDecl_External[] = // 56 bytes
{
	{0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
	{0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
	{0, 24, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  0},
	{0, 36, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 0},
	{0, 48, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
	D3DDECL_END()
};

#pragma pack(push,1)
struct vertExternal
{
	float P[3];
	float N[3];
	float T[3];
	float B[3];
	float tc[2];
};
#pragma pack(pop)

static IC void ext_set_vertex(vertExternal& dst, const Fvector& P, Fvector N, Fvector T, Fvector B, float u, float v)
{
	N.normalize_safe();
	T.normalize_safe();
	B.normalize_safe();
	dst.P[0] = P.x;  dst.P[1] = P.y;  dst.P[2] = P.z;
	dst.N[0] = N.x;  dst.N[1] = N.y;  dst.N[2] = N.z;
	dst.T[0] = T.x;  dst.T[1] = T.y;  dst.T[2] = T.z;
	dst.B[0] = B.x;  dst.B[1] = B.y;  dst.B[2] = B.z;
	dst.tc[0] = u;   dst.tc[1] = v;
}

//////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////

// Turn a glTF image URI into an X-Ray texture name (relative to $game_textures$, no
// extension). Returns false for embedded data: URIs or missing images.
static bool ext_texture_name_from_uri(const char* uri, string_path out)
{
	if (!uri || !uri[0])
		return false;
	if (0 == strncmp(uri, "data:", 5))
		return false; // base64-embedded image: no usable name

	xr_strcpy(out, sizeof(string_path), uri);
	// normalize separators
	for (char* p = out; *p; ++p)
		if (*p == '/') *p = '\\';
	// strip extension
	if (char* e = strext(out)) *e = 0;
	return out[0] != 0;
}

//////////////////////////////////////////////////////////////////////
// Construction / Destruction
//////////////////////////////////////////////////////////////////////

FExternalVisual::FExternalVisual() : dxRender_Visual()
{
}

FExternalVisual::~FExternalVisual()
{
	HW.stats_manager.decrement_stats_vb(p_rm_Vertices);
	HW.stats_manager.decrement_stats_ib(p_rm_Indices);
	// p_rm_Vertices / p_rm_Indices are released by ~IRender_Mesh().
}

void FExternalVisual::Release()
{
	dxRender_Visual::Release();
}

void FExternalVisual::Load(LPCSTR N, IReader* /*data*/, u32 /*dwFlags*/)
{
	// External visuals are populated by LoadExternal(), never through the OGF path.
	Msg("! FExternalVisual::Load() called unexpectedly for [%s] - external visuals load via LoadExternal()", N);
}

//////////////////////////////////////////////////////////////////////
// External loader (Phase 1: single merged static mesh)
//////////////////////////////////////////////////////////////////////

bool FExternalVisual::LoadExternal(const char* short_name, const char* full_path)
{
	dbg_name = short_name;
	dbg_id = 1;
	skinning = -1; // SKIN_NONE: m_skinning < 0 selects the static v_model VS path (r4.cpp:1487)
	hud = false;

	// --- read the whole file through the engine VFS (works for loose and packed) -----
	IReader* rd = FS.r_open(full_path);
	if (!rd)
	{
		Msg("! [gltf] can't open '%s'", full_path);
		return false;
	}
	const size_t blob_size = (size_t)rd->length();
	void* blob = xr_malloc(blob_size);
	CopyMemory(blob, rd->pointer(), blob_size);
	FS.r_close(rd);

	// --- parse ----------------------------------------------------------------------
	cgltf_options options = {};
	cgltf_data* gltf = NULL;
	cgltf_result res = cgltf_parse(&options, blob, blob_size, &gltf);
	if (res != cgltf_result_success)
	{
		Msg("! [gltf] parse failed (%d) for '%s'", int(res), full_path);
		xr_free(blob);
		return false;
	}
	// resolve buffers (embedded GLB bin / base64 / external .bin via full_path)
	res = cgltf_load_buffers(&options, gltf, full_path);
	if (res != cgltf_result_success)
	{
		Msg("! [gltf] load_buffers failed (%d) for '%s'", int(res), full_path);
		cgltf_free(gltf);
		xr_free(blob);
		return false;
	}

	// --- accumulate all triangle primitives into one VB/IB --------------------------
	xr_vector<vertExternal> verts;
	xr_vector<u16> indices;
	Fbox bb;
	bb.invalidate();
	const char* base_tex_uri = NULL;

	for (cgltf_size mi = 0; mi < gltf->meshes_count; ++mi)
	{
		const cgltf_mesh& mesh = gltf->meshes[mi];
		for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
		{
			const cgltf_primitive& prim = mesh.primitives[pi];
			if (prim.type != cgltf_primitive_type_triangles)
				continue;

			// locate attributes
			const cgltf_accessor* a_pos = NULL;
			const cgltf_accessor* a_nrm = NULL;
			const cgltf_accessor* a_uv = NULL;
			const cgltf_accessor* a_tan = NULL;
			for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
			{
				const cgltf_attribute& at = prim.attributes[ai];
				switch (at.type)
				{
				case cgltf_attribute_type_position: a_pos = at.data; break;
				case cgltf_attribute_type_normal: a_nrm = at.data; break;
				case cgltf_attribute_type_texcoord: if (at.index == 0) a_uv = at.data; break;
				case cgltf_attribute_type_tangent: a_tan = at.data; break;
				default: break;
				}
			}
			if (!a_pos)
				continue;

			const u32 v_base = (u32)verts.size();
			const cgltf_size v_count = a_pos->count;

			// overflow guard: Phase 1 uses 16-bit indices like the OGF model path
			if (v_base + v_count > 65535)
			{
				Msg("! [gltf] '%s' exceeds 65535 vertices (16-bit index limit); Phase 1 cannot load it", full_path);
				cgltf_free(gltf);
				xr_free(blob);
				return false;
			}

			verts.reserve(verts.size() + v_count);
			for (cgltf_size i = 0; i < v_count; ++i)
			{
				Fvector P, N, T;
				P.set(0, 0, 0);
				N.set(0, 1, 0); // default up-normal if the mesh has no normals
				T.set(1, 0, 0);
				float uv[2] = {0, 0};
				float tan4[4] = {1, 0, 0, 1};

				cgltf_accessor_read_float(a_pos, i, &P.x, 3);
				if (a_nrm) cgltf_accessor_read_float(a_nrm, i, &N.x, 3);
				if (a_uv) cgltf_accessor_read_float(a_uv, i, uv, 2);
				if (a_tan)
				{
					cgltf_accessor_read_float(a_tan, i, tan4, 4);
					T.set(tan4[0], tan4[1], tan4[2]);
				}

#if EXTERNAL_FLIP_Z
				P.z = -P.z;
				N.z = -N.z;
				T.z = -T.z;
#endif
				Fvector B;
				B.crossproduct(N, T);
				B.mul(tan4[3]); // glTF tangent handedness

				vertExternal vx;
				ext_set_vertex(vx, P, N, T, B, uv[0], uv[1]);
				verts.push_back(vx);
				bb.modify(P);
			}

			// indices (offset by v_base); reverse nothing - see EXTERNAL_FLIP_Z note
			if (prim.indices)
			{
				const cgltf_size n = prim.indices->count;
				indices.reserve(indices.size() + n);
				for (cgltf_size i = 0; i < n; ++i)
					indices.push_back((u16)(v_base + cgltf_accessor_read_index(prim.indices, i)));
			}
			else
			{
				indices.reserve(indices.size() + v_count);
				for (cgltf_size i = 0; i < v_count; ++i)
					indices.push_back((u16)(v_base + i));
			}

			// remember a base-color texture name from the first material we see
			if (!base_tex_uri && prim.material && prim.material->has_pbr_metallic_roughness)
			{
				const cgltf_texture* t = prim.material->pbr_metallic_roughness.base_color_texture.texture;
				if (t && t->image && t->image->uri)
					base_tex_uri = t->image->uri;
			}
		}
	}

	// copy texture uri out before freeing gltf (it points into gltf-owned memory)
	string_path tex_name;
	bool have_tex = ext_texture_name_from_uri(base_tex_uri, tex_name);

	cgltf_free(gltf);
	xr_free(blob);

	if (verts.empty() || indices.empty())
	{
		Msg("! [gltf] '%s' has no triangle geometry", full_path);
		return false;
	}

	// --- GPU buffers (mirror Fvisual) -----------------------------------------------
	vBase = 0;
	vCount = (u32)verts.size();
	iBase = 0;
	iCount = (u32)indices.size();
	dwPrimitives = iCount / 3;

	const u32 vStride = sizeof(vertExternal);
	VERIFY(vStride == (u32)D3DXGetDeclVertexSize(dwDecl_External, 0));

#if defined(USE_DX10) || defined(USE_DX11)
	VERIFY(NULL == p_rm_Vertices);
	R_CHK(dx10BufferUtils::CreateVertexBuffer(&p_rm_Vertices, verts.data(), vCount * vStride));
	HW.stats_manager.increment_stats_vb(p_rm_Vertices);

	VERIFY(NULL == p_rm_Indices);
	R_CHK(dx10BufferUtils::CreateIndexBuffer(&p_rm_Indices, indices.data(), iCount * 2));
	HW.stats_manager.increment_stats_ib(p_rm_Indices);
#else // DX9
	{
		BOOL bSoft = HW.Caps.geometry.bSoftware;
		u32 dwUsage = D3DUSAGE_WRITEONLY | (bSoft ? D3DUSAGE_SOFTWAREPROCESSING : 0);
		BYTE* bytes = 0;
		VERIFY(NULL == p_rm_Vertices);
		R_CHK(HW.pDevice->CreateVertexBuffer(vCount * vStride, dwUsage, 0, D3DPOOL_MANAGED, &p_rm_Vertices, 0));
		HW.stats_manager.increment_stats_vb(p_rm_Vertices);
		R_CHK(p_rm_Vertices->Lock(0, 0, (void**)&bytes, 0));
		CopyMemory(bytes, verts.data(), vCount * vStride);
		p_rm_Vertices->Unlock();
	}
	{
		BOOL bSoft = HW.Caps.geometry.bSoftware;
		u32 dwUsage = (bSoft ? D3DUSAGE_SOFTWAREPROCESSING : 0);
		BYTE* bytes = 0;
		VERIFY(NULL == p_rm_Indices);
		R_CHK(HW.pDevice->CreateIndexBuffer(iCount * 2, dwUsage, D3DFMT_INDEX16, D3DPOOL_MANAGED, &p_rm_Indices, 0));
		HW.stats_manager.increment_stats_ib(p_rm_Indices);
		R_CHK(p_rm_Indices->Lock(0, 0, (void**)&bytes, 0));
		CopyMemory(bytes, indices.data(), iCount * 2);
		p_rm_Indices->Unlock();
	}
#endif

	rm_geom.create(dwDecl_External, p_rm_Vertices, p_rm_Indices);

	// --- bounding volumes -----------------------------------------------------------
	vis.box.set(bb.min, bb.max);
	Fvector c;
	c.add(bb.min, bb.max).mul(0.5f);
	Fvector half;
	half.sub(bb.max, bb.min).mul(0.5f);
	vis.sphere.set(c, half.magnitude());

	// --- material / shader ----------------------------------------------------------
	// Fall back to the model's own name if the glTF gives no usable texture.
	if (!have_tex)
	{
		xr_strcpy(tex_name, sizeof(tex_name), short_name);
		if (char* e = strext(tex_name)) *e = 0;
	}
	SetShaderTexture(EXTERNAL_DEFAULT_SHADER, tex_name);

	Type = MT_EXTERNAL_STATIC;
	return true;
}

//////////////////////////////////////////////////////////////////////
// Render
//////////////////////////////////////////////////////////////////////

void FExternalVisual::Render(float)
{
	PROF_EVENT("FExternalVisual::Render");
	RCache.set_Geometry(rm_geom);
	RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, iBase, dwPrimitives);
	RCache.stat.r.s_static.add(vCount);
}

//////////////////////////////////////////////////////////////////////
// Copy (for Instance_Duplicate / instancing)
//////////////////////////////////////////////////////////////////////

#define PCOPY(a)	a = pFrom->a

void FExternalVisual::Copy(dxRender_Visual* pSrc)
{
	dxRender_Visual::Copy(pSrc);

	FExternalVisual* pFrom = fast_dynamic_cast<FExternalVisual*>(pSrc);
	VERIFY(pFrom);

	PCOPY(rm_geom);

	PCOPY(p_rm_Vertices);
	if (p_rm_Vertices) p_rm_Vertices->AddRef();
	PCOPY(vBase);
	PCOPY(vCount);

	PCOPY(p_rm_Indices);
	if (p_rm_Indices) p_rm_Indices->AddRef();
	PCOPY(iBase);
	PCOPY(iCount);
	PCOPY(dwPrimitives);
}
