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

// D3DX10/11 image-decode-from-memory (PNG/JPG/BMP/TGA/DDS + mip generation). Same library and
// include that dx10Texture.cpp uses for CRender::texture_load; only meaningful on DX10/DX11.
#if defined(USE_DX11) || defined(USE_DX10)
#include <D3DX10Tex.h>
#endif

#include "../../xrEngine/fmesh.h"
#include "FExternalVisual.h"

#include "../xrRenderDX10/dx10BufferUtils.h"

#include "cgltf.h"

//////////////////////////////////////////////////////////////////////
// Configuration
//////////////////////////////////////////////////////////////////////

// glTF is right-handed (Y-up); X-Ray is left-handed (Y-up). Negating Z converts the handedness
// of positions/normals. Mirroring also reverses triangle orientation, so we reverse the winding
// (EXTERNAL_FLIP_WINDING) to keep front faces facing OUT -- otherwise the mesh renders inside-out
// (front faces culled => the box looks see-through). Toggle either if a given exporter authors
// left-handed data or the result looks inverted.
#define EXTERNAL_FLIP_Z 1
#define EXTERNAL_FLIP_WINDING 1

// Auto-scale imported models into a sane size band. Authored glTF assets use arbitrary unit scales:
// some are tens of units across (a "rubber duck"), others are fractions of a unit (a ~0.1u avocado),
// so they spawn comically large OR comically tiny. If the loaded model's largest bounding-box
// dimension is above EXTERNAL_AUTOSCALE_MAX_SIZE it's uniformly scaled down; if it's below
// EXTERNAL_AUTOSCALE_MIN_SIZE it's scaled up; models already in [MIN,MAX] are left untouched. Scale
// is about the local origin so the bbox / physics box stay consistent. Set EXTERNAL_AUTOSCALE 0 to off.
#define EXTERNAL_AUTOSCALE 1
#define EXTERNAL_AUTOSCALE_MIN_SIZE 0.5f
#define EXTERNAL_AUTOSCALE_MAX_SIZE 2.0f

// Phase 1 shader (gamedata/shaders/r3/external_static.s). It reuses the stock deferred
// MODEL vertex/pixel shaders, which consume the exact D3DCOLOR-packed model vertex layout
// produced below, and renders the mesh fullbright (emissive) so it is visible regardless of
// scene lighting -- ideal for confirming geometry/UVs. Phase 2 swaps in a lit pbr_external.
#define EXTERNAL_DEFAULT_SHADER "external_static"

// Lit + normal-mapped variant, used instead of EXTERNAL_DEFAULT_SHADER when the glTF material has a
// normal texture. Receives a "albedo,normal" texture list (t_base = albedo, t_second = normal map).
#define EXTERNAL_BUMP_SHADER "external_bump"

// Lit + normal + metallic-roughness variant, used when the material also has a metallic-roughness
// map. Texture list is still "albedo,normal"; the metal-rough texture is registered under a derived
// "$user$gltf_mr\..." name that external_bump_mr.s reconstructs from the normal map's name.
#define EXTERNAL_BUMP_MR_SHADER "external_bump_mr"

// Lit + metallic-roughness but NO normal map (e.g. smooth PBR spheres). Uses the flat VS + vertex
// normals; gets a "albedo,metalrough" list (t_base / t_second) -- no derivation needed (2 textures).
#define EXTERNAL_MR_SHADER "external_mr"

// Engine missing-texture placeholder (ships with the base game, always present). Bound when the
// glTF has no usable base-color texture, or the named texture isn't on disk -- X-Ray FATALS on a
// missing texture ("Can't find texture ..."), so we must never bind a name that does not resolve.
#define EXTERNAL_FALLBACK_TEXTURE "ed\\ed_not_existing_texture"

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

// True if a texture "<name>.dds" exists under $game_textures$. X-Ray fatals on a missing texture,
// so the loader checks this before binding a glTF-provided base-color name.
static bool ext_texture_exists(const char* name)
{
	if (!name || !name[0])
		return false;
	string_path fn, with_ext;
	strconcat(sizeof(with_ext), with_ext, name, ".dds");
	return !!FS.exist(fn, "$game_textures$", with_ext);
}

//////////////////////////////////////////////////////////////////////
// Embedded / external base-color texture decoding
//
// glTF carries its base-color image one of three ways: (1) a GLB binary chunk referenced by an
// image bufferView (the usual case for .glb), (2) an external image file referenced by URI, or
// (3) a base64 "data:" URI. We reuse the D3DX11 image decoder the engine already links (see
// dx10Texture.cpp) -- it loads PNG/JPG/BMP/TGA/DDS from memory and builds a full mip chain. The
// decoded texture is registered under a synthetic "$user$..." name; CTexture::Load() short-circuits
// for "$user$" names (no disk I/O, see dx10SH_Texture.cpp), so the surface we set is what binds.
//////////////////////////////////////////////////////////////////////

// Decode an in-memory image into a GPU texture (with mips). Caller releases the returned surface.
// Returns NULL on DX9 (no D3DX10/11) or on decode failure, so the caller falls back to a .dds /
// the placeholder.
static ID3DBaseTexture* ext_create_texture_from_memory(const void* data, size_t size, const char* dbg)
{
	if (!data || !size)
		return NULL;
#if defined(USE_DX11)
	D3DX11_IMAGE_LOAD_INFO li; // default ctor: full mip chain, format from file, BIND_SHADER_RESOURCE
	ID3DBaseTexture* tex = NULL;
	HRESULT hr = D3DX11CreateTextureFromMemory(HW.pDevice, data, (SIZE_T)size, &li, NULL, &tex, NULL);
	if (FAILED(hr) || !tex) { Msg("! [gltf] image decode failed (0x%08x): %s", hr, dbg ? dbg : ""); return NULL; }
	return tex;
#elif defined(USE_DX10)
	D3DX10_IMAGE_LOAD_INFO li;
	ID3DBaseTexture* tex = NULL;
	HRESULT hr = D3DX10CreateTextureFromMemory(HW.pDevice, data, (SIZE_T)size, &li, NULL, &tex, NULL);
	if (FAILED(hr) || !tex) { Msg("! [gltf] image decode failed (0x%08x): %s", hr, dbg ? dbg : ""); return NULL; }
	return tex;
#else
	(void)dbg;
	return NULL; // DX9 path: embedded-texture decode unsupported; caller uses .dds / placeholder
#endif
}

// Synthetic resource name for a model's decoded base texture (unique per model -> no collisions).
static void ext_make_user_name(string_path out, const char* short_name)
{
	strconcat(sizeof(string_path), out, "$user$gltf\\", short_name ? short_name : "unnamed");
}

// Synthetic resource name for a model's decoded normal map (distinct namespace from the albedo).
static void ext_make_user_name_n(string_path out, const char* short_name)
{
	strconcat(sizeof(string_path), out, "$user$gltf_n\\", short_name ? short_name : "unnamed");
}

// Synthetic resource name for a model's decoded metallic-roughness map. MUST stay in sync with the
// "gltf_n"->"gltf_mr" derivation in external_bump_mr.s (which rebuilds this from the normal name).
static void ext_make_user_name_mr(string_path out, const char* short_name)
{
	strconcat(sizeof(string_path), out, "$user$gltf_mr\\", short_name ? short_name : "unnamed");
}

// Fetch the bytes for a glTF image and decode them. Handles GLB/.bin bufferView images and external
// image files (resolved relative to the glTF). base64 "data:" URIs are not decoded yet.
static ID3DBaseTexture* ext_decode_gltf_image(const cgltf_image* image, const char* gltf_full_path)
{
	if (!image)
		return NULL;

	// (1) image embedded in a buffer view (GLB binary chunk, or .gltf + .bin)
	if (image->buffer_view)
	{
		const cgltf_buffer_view* bv = image->buffer_view;
		const uint8_t* p = cgltf_buffer_view_data(bv);
		if (!p || !bv->size)
			return NULL;
		return ext_create_texture_from_memory(p, (size_t)bv->size, gltf_full_path);
	}

	// (2) external image file referenced by URI (skip base64 data: URIs)
	if (image->uri && image->uri[0] && 0 != strncmp(image->uri, "data:", 5))
	{
		// directory the glTF lives in
		string_path dir;
		xr_strcpy(dir, gltf_full_path);
		char* slash = strrchr(dir, '\\');
		char* slash2 = strrchr(dir, '/');
		if (slash2 > slash) slash = slash2;
		if (slash) slash[1] = 0; else dir[0] = 0;

		// percent-decode the URI in a local copy, normalize separators
		string_path uri;
		xr_strcpy(uri, image->uri);
		cgltf_decode_uri(uri);
		for (char* c = uri; *c; ++c) if (*c == '/') *c = '\\';

		string_path path;
		strconcat(sizeof(path), path, dir, uri);

		IReader* rd = FS.r_open(path);
		if (!rd) { Msg("~ [gltf] external image '%s' not found", path); return NULL; }
		const size_t sz = (size_t)rd->length();
		void* buf = xr_malloc(sz);
		CopyMemory(buf, rd->pointer(), sz);
		FS.r_close(rd);
		ID3DBaseTexture* tex = ext_create_texture_from_memory(buf, sz, path);
		xr_free(buf);
		return tex;
	}

	// (3) base64 data: URI image -- not handled yet (falls back to .dds / placeholder)
	return NULL;
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
	xr_vector<u32> indices; // 32-bit accumulator; packed down to 16-bit at buffer-creation if it fits
	Fbox bb;
	bb.invalidate();
	const char* base_tex_uri = NULL;
	const cgltf_image* base_img = NULL;
	const cgltf_image* normal_img = NULL;
	const cgltf_image* mr_img = NULL;

	// Iterate the scene graph so node transforms (translate/rotate/scale) are baked into the
	// geometry. Real exported models position meshes via nodes; ignoring them mis-places/scales
	// the mesh. (A glTF with meshes but no node referencing them is unusual and emits nothing.)
	for (cgltf_size ni = 0; ni < gltf->nodes_count; ++ni)
	{
		const cgltf_node& node = gltf->nodes[ni];
		if (!node.mesh)
			continue;
		cgltf_float _world[16];
		cgltf_node_transform_world(&node, _world);
		Fmatrix Mw;
		// glTF (column-major, column-vector) vs X-Ray (row-major, row-vector): the two transposes
		// cancel, so the 16 floats map directly onto the row-major Fmatrix.
		memcpy(&Mw, _world, sizeof(_world));
		const cgltf_mesh& mesh = *node.mesh;
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

				// bake the node world transform (glTF space): position full, normal/tangent as dirs
				{ Fvector t; Mw.transform_tiny(t, P); P = t; }
				{ Fvector t; Mw.transform_dir(t, N); N = t; }
				{ Fvector t; Mw.transform_dir(t, T); T = t; }

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

			// indices (offset by v_base), per triangle, with optional winding reversal to match
			// the engine's left-handed front-face convention (see EXTERNAL_FLIP_WINDING note).
			{
				const cgltf_size n = prim.indices ? prim.indices->count : v_count;
				indices.reserve(indices.size() + n);
				for (cgltf_size i = 0; i + 3 <= n; i += 3)
				{
					const u32 a = (u32)(v_base + (prim.indices ? cgltf_accessor_read_index(prim.indices, i + 0) : i + 0));
					const u32 b = (u32)(v_base + (prim.indices ? cgltf_accessor_read_index(prim.indices, i + 1) : i + 1));
					const u32 c = (u32)(v_base + (prim.indices ? cgltf_accessor_read_index(prim.indices, i + 2) : i + 2));
#if EXTERNAL_FLIP_WINDING
					indices.push_back(a); indices.push_back(c); indices.push_back(b);
#else
					indices.push_back(a); indices.push_back(b); indices.push_back(c);
#endif
				}
			}

			// remember the first textured material's base-color image (for embedded/external decode)
			if (!base_img && prim.material && prim.material->has_pbr_metallic_roughness)
			{
				const cgltf_texture* t = prim.material->pbr_metallic_roughness.base_color_texture.texture;
				if (t && t->image)
				{
					base_img = t->image;
					if (t->image->uri) base_tex_uri = t->image->uri; // also usable as a pre-converted .dds name
				}
			}

			// remember the first material's normal map (selects the bump shader when present)
			if (!normal_img && prim.material && prim.material->normal_texture.texture)
			{
				const cgltf_texture* nt = prim.material->normal_texture.texture;
				if (nt->image) normal_img = nt->image;
			}

			// remember the first material's metallic-roughness map (-> gloss via the bump_mr shader)
			if (!mr_img && prim.material && prim.material->has_pbr_metallic_roughness)
			{
				const cgltf_texture* mt = prim.material->pbr_metallic_roughness.metallic_roughness_texture.texture;
				if (mt && mt->image) mr_img = mt->image;
			}
		}
	}

	// Resolve the base-color image into a GPU texture BEFORE freeing gltf -- the image bytes live in
	// gltf-owned buffer memory. Also copy the URI-derived name for the pre-converted-.dds fallback.
	string_path tex_name;
	bool have_tex = ext_texture_name_from_uri(base_tex_uri, tex_name);
	ID3DBaseTexture* decoded_tex = base_img ? ext_decode_gltf_image(base_img, full_path) : NULL;
	ID3DBaseTexture* decoded_nrm = normal_img ? ext_decode_gltf_image(normal_img, full_path) : NULL;
	ID3DBaseTexture* decoded_mr = mr_img ? ext_decode_gltf_image(mr_img, full_path) : NULL;

	cgltf_free(gltf);
	xr_free(blob);

	if (verts.empty() || indices.empty())
	{
		Msg("! [gltf] '%s' has no triangle geometry", full_path);
		return false;
	}

	// --- auto-scale oversized models -------------------------------------------------
	// Cap the model's largest dimension at the player-ish target so wrongly-scaled assets don't
	// spawn comically large. Uniform scale about the local origin -> the bbox below (and the
	// physics box FExternalKinematics builds from it) stay consistent. Never scales models UP.
#if EXTERNAL_AUTOSCALE
	{
		float maxdim = bb.max.x - bb.min.x;
		const float dy = bb.max.y - bb.min.y;
		const float dz = bb.max.z - bb.min.z;
		if (dy > maxdim) maxdim = dy;
		if (dz > maxdim) maxdim = dz;

		float s = 1.0f;
		if (maxdim > EXTERNAL_AUTOSCALE_MAX_SIZE)
			s = EXTERNAL_AUTOSCALE_MAX_SIZE / maxdim;                    // too big -> shrink to player-ish
		else if (maxdim > 1e-4f && maxdim < EXTERNAL_AUTOSCALE_MIN_SIZE)
			s = EXTERNAL_AUTOSCALE_MIN_SIZE / maxdim;                    // too small -> grow to visible size

		if (s != 1.0f)
		{
			for (u32 vi = 0; vi < verts.size(); ++vi)
			{
				verts[vi].P[0] *= s;
				verts[vi].P[1] *= s;
				verts[vi].P[2] *= s;
			}
			bb.min.mul(s);
			bb.max.mul(s);
			Msg("~ [gltf] '%s' auto-scaled x%.4f (largest dim %.2f -> %.2f units)", full_path, s, maxdim, maxdim * s);
		}
	}
#endif

	// --- GPU buffers (mirror Fvisual) -----------------------------------------------
	vBase = 0;
	vCount = (u32)verts.size();
	iBase = 0;
	iCount = (u32)indices.size();
	dwPrimitives = iCount / 3;

	// >65535 vertices can't be addressed by 16-bit indices, so keep the index buffer 32-bit.
	m_index32 = (vCount > 65535);

	const u32 vStride = sizeof(vertExternal);
	VERIFY(vStride == (u32)D3DXGetDeclVertexSize(dwDecl_External, 0));

#if defined(USE_DX10) || defined(USE_DX11)
	VERIFY(NULL == p_rm_Vertices);
	R_CHK(dx10BufferUtils::CreateVertexBuffer(&p_rm_Vertices, verts.data(), vCount * vStride));
	HW.stats_manager.increment_stats_vb(p_rm_Vertices);

	VERIFY(NULL == p_rm_Indices);
	if (m_index32)
	{
		// keep 32-bit indices; Render() rebinds the IB as R32_UINT (set_Indices defaults to R16)
		R_CHK(dx10BufferUtils::CreateIndexBuffer(&p_rm_Indices, indices.data(), iCount * 4));
	}
	else
	{
		// pack down to the engine-default 16-bit so the standard R16_UINT bind is correct
		xr_vector<u16> i16;
		i16.resize(iCount);
		for (u32 k = 0; k < iCount; ++k) i16[k] = (u16)indices[k];
		R_CHK(dx10BufferUtils::CreateIndexBuffer(&p_rm_Indices, i16.data(), iCount * 2));
	}
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
		if (m_index32)
		{
			// DX9 bakes the index format into the buffer, so no per-draw rebind is needed here.
			R_CHK(HW.pDevice->CreateIndexBuffer(iCount * 4, dwUsage, D3DFMT_INDEX32, D3DPOOL_MANAGED, &p_rm_Indices, 0));
			HW.stats_manager.increment_stats_ib(p_rm_Indices);
			R_CHK(p_rm_Indices->Lock(0, 0, (void**)&bytes, 0));
			CopyMemory(bytes, indices.data(), iCount * 4);
			p_rm_Indices->Unlock();
		}
		else
		{
			xr_vector<u16> i16;
			i16.resize(iCount);
			for (u32 k = 0; k < iCount; ++k) i16[k] = (u16)indices[k];
			R_CHK(HW.pDevice->CreateIndexBuffer(iCount * 2, dwUsage, D3DFMT_INDEX16, D3DPOOL_MANAGED, &p_rm_Indices, 0));
			HW.stats_manager.increment_stats_ib(p_rm_Indices);
			R_CHK(p_rm_Indices->Lock(0, 0, (void**)&bytes, 0));
			CopyMemory(bytes, i16.data(), iCount * 2);
			p_rm_Indices->Unlock();
		}
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
	// Preferred: the runtime texture decoded from the glTF's embedded/external base-color image,
	// registered under a "$user$" name so the engine binds it without touching disk. Fallbacks, in
	// order: a pre-converted .dds already in $game_textures$, then the engine missing-texture
	// placeholder (X-Ray FATALS on a missing texture, so the bound name must always resolve).
	ref_texture user_tex; // must outlive SetShaderTexture so the shader can take its own ref
	ref_texture user_nrm; // ditto, for the normal map
	ref_texture user_mr;  // ditto, for the metallic-roughness map
	bool resolved = false;

	if (decoded_tex)
	{
		ext_make_user_name(tex_name, short_name);
		user_tex.create(tex_name);
		if (user_tex._get())
		{
			user_tex->surface_set(decoded_tex);
			resolved = true;
		}
		_RELEASE(decoded_tex);
	}

	if (!resolved && have_tex && ext_texture_exists(tex_name))
		resolved = true;

	if (!resolved)
	{
		if (have_tex)
			Msg("~ [gltf] base texture '%s' not found; using placeholder for '%s'", tex_name, full_path);
		xr_strcpy(tex_name, sizeof(tex_name), EXTERNAL_FALLBACK_TEXTURE);
	}

	// Decode the normal map into its own $user$ texture. When present, switch to the bump shader and
	// hand it a "albedo,normal" texture list (-> t_base / t_second). glTF normal maps are linear, so
	// the default (non-sRGB) decode is correct here.
	string_path normal_name;
	bool have_normal = false;
	if (decoded_nrm)
	{
		ext_make_user_name_n(normal_name, short_name);
		user_nrm.create(normal_name);
		if (user_nrm._get())
		{
			user_nrm->surface_set(decoded_nrm);
			have_normal = true;
		}
		_RELEASE(decoded_nrm);
	}

	// Metallic-roughness map -> its own $user$ texture (decode is linear like the normal map). Created
	// regardless of a normal map: with a normal it feeds external_bump_mr (name derived in shader),
	// without one it feeds external_mr (name passed explicitly).
	bool have_mr = false;
	string_path mr_name;
	if (decoded_mr)
	{
		ext_make_user_name_mr(mr_name, short_name);
		user_mr.create(mr_name);
		if (user_mr._get())
		{
			user_mr->surface_set(decoded_mr);
			have_mr = true;
		}
		_RELEASE(decoded_mr);
	}

	string_path tlist;
	if (have_normal && have_mr)
	{
		strconcat(sizeof(tlist), tlist, tex_name, ",", normal_name); // MR name derived in the shader
		SetShaderTexture(EXTERNAL_BUMP_MR_SHADER, tlist);
	}
	else if (have_normal)
	{
		strconcat(sizeof(tlist), tlist, tex_name, ",", normal_name);
		SetShaderTexture(EXTERNAL_BUMP_SHADER, tlist);
	}
	else if (have_mr)
	{
		strconcat(sizeof(tlist), tlist, tex_name, ",", mr_name);     // albedo + metal-rough (no normal)
		SetShaderTexture(EXTERNAL_MR_SHADER, tlist);
	}
	else
	{
		SetShaderTexture(EXTERNAL_DEFAULT_SHADER, tex_name);
	}

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
#if defined(USE_DX11) || defined(USE_DX10)
	// set_Geometry() just bound the IB as R16_UINT (the backend hardcodes that format). For a 32-bit
	// mesh rebind it as R32_UINT before the draw; CBackend::Render() issues DrawIndexed without
	// re-touching the IB, so this override sticks. (DX9 bakes the format into the IB at creation.)
	if (m_index32)
		HW.pContext->IASetIndexBuffer(p_rm_Indices, DXGI_FORMAT_R32_UINT, 0);
#endif
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
	PCOPY(m_index32);
}
