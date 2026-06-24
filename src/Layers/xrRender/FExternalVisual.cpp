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
#define EXTERNAL_AUTOSCALE_MIN_SIZE 0.01f // 3.4 -- grow only ABSURDLY tiny models; real meters honored otherwise
#define EXTERNAL_AUTOSCALE_MAX_SIZE 50.0f  // 3.4 -- shrink only ABSURDLY large models

// Phase 1 shader (gamedata/shaders/r3/external_static.s). It reuses the stock deferred
// MODEL vertex/pixel shaders, which consume the exact D3DCOLOR-packed model vertex layout
// produced below, and renders the mesh fullbright (emissive) so it is visible regardless of
// scene lighting -- ideal for confirming geometry/UVs. Phase 2 swaps in a lit pbr_external.
#define EXTERNAL_DEFAULT_SHADER "external_static"

// Skinned variant of EXTERNAL_DEFAULT_SHADER. Identical bindings, but a SEPARATE .s name so it isn't
// reused from the static path's cached SKIN_NONE compile -- the compiled .s/VS is cached by name and the
// skin mode is baked in at first compile. FExternalSkinned loads this (always with SetSkinningMode(4)) so
// it compiles SKIN_4 and the model VS actually reads sbones_array. See gamedata\shaders\r3\external_skinned.s.
#define EXTERNAL_SKINNED_SHADER "external_skinned"

// Vertex-coloured variant (glTF COLOR_0, no base texture): uses a custom VS that passes the per-vertex
// colour and a PS that uses it as albedo. Selected when a material has vertex colours but no normal/MR
// texture (e.g. BoxVertexColors). See external_vc.s / deffer_base_ext_vc.ps / deffer_model_flat_vc.vs.
#define EXTERNAL_VC_SHADER "external_vc"

// Lit + normal-mapped variant, used instead of EXTERNAL_DEFAULT_SHADER when the glTF material has a
// normal texture. Receives a "albedo,normal" texture list (t_base = albedo, t_second = normal map).
#define EXTERNAL_BUMP_SHADER "external_bump"

// Lit + normal + metallic-roughness variant, used when the material has both a normal map and a
// metallic-roughness map. All three are passed explicitly: "albedo,normal,metalrough".
#define EXTERNAL_BUMP_MR_SHADER "external_bump_mr"

// Lit + metallic-roughness but NO normal map (e.g. smooth PBR spheres). Uses the flat VS + vertex
// normals; gets a "albedo,metalrough" list (t_base / t_second) -- no derivation needed (2 textures).
#define EXTERNAL_MR_SHADER "external_mr"

// Double-sided (no backface cull) variants of the four deferred lit shaders, selected when the glTF
// material has doubleSided=true. Same bindings as the single-sided ones; the .s sets dx10cullmode NONE
// and the PS flips the shading normal on back faces via SV_IsFrontFace. (3.2)
#define EXTERNAL_DEFAULT_DS_SHADER "external_static_ds"
#define EXTERNAL_MR_DS_SHADER      "external_mr_ds"
#define EXTERNAL_BUMP_DS_SHADER    "external_bump_ds"
#define EXTERNAL_BUMP_MR_DS_SHADER "external_bump_mr_ds"

// Forward additive emissive OVERLAY (rendered in addition to the lit batch, post-deferred). Gets the
// emissive map as t_base. See external_emissive.s / deffer_base_ext_emissive.ps.
#define EXTERNAL_EMISSIVE_SHADER "external_emissive"

// Forward additive colored-REFLECTION (metal) OVERLAY. Gets "albedo,metalrough" (t_base / t_second) and
// reflects the sky cubes tinted by albedo, masked by metalness. See external_metal.s /
// deffer_base_ext_metal.ps. This is the stock-safe half of glTF metalness (the deferred half -- diffuse
// suppression -- lives in deffer_base_ext_*_mr.ps).
#define EXTERNAL_METAL_SHADER "external_metal"

// Forward LIT, alpha-BLENDED pass for glTF alphaMode=BLEND materials. Gets the albedo (rgb + opacity in
// .a) as t_base; renders forward (src-alpha blend, back-to-front) INSTEAD of the deferred batch, since a
// transparent surface can't go in the G-buffer. See external_blend.s / deffer_base_ext_blend.ps.
#define EXTERNAL_BLEND_SHADER "external_blend"

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

static D3DVERTEXELEMENT9 dwDecl_External[] = // 80 bytes
{
	{0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
	{0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
	{0, 24, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  0},
	{0, 36, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 0},
	{0, 48, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, // UV0
	{0, 56, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1}, // UV1 (glTF TEXCOORD_1)
	// glTF COLOR_0 (per-vertex colour). Always present (default white); the external VSes read it (the
	// stock flat/bump VSes, if ever used, simply ignore this extra element).
	{0, 64, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
	D3DDECL_END()
};

#pragma pack(push,1)
struct vertExternal
{
	float P[3];
	float N[3];
	float T[3];
	float B[3];
	float tc[2];  // UV0
	float tc1[2]; // UV1 (glTF TEXCOORD_1); == UV0 when the mesh has no 2nd UV set
	float C[4];   // glTF COLOR_0 (RGBA), default white
};
#pragma pack(pop)

static IC void ext_set_vertex(vertExternal& dst, const Fvector& P, Fvector N, Fvector T, Fvector B,
                              float u, float v, float u1, float v1, const float col[4])
{
	N.normalize_safe();
	T.normalize_safe();
	B.normalize_safe();
	dst.P[0] = P.x;  dst.P[1] = P.y;  dst.P[2] = P.z;
	dst.N[0] = N.x;  dst.N[1] = N.y;  dst.N[2] = N.z;
	dst.T[0] = T.x;  dst.T[1] = T.y;  dst.T[2] = T.z;
	dst.B[0] = B.x;  dst.B[1] = B.y;  dst.B[2] = B.z;
	dst.tc[0] = u;   dst.tc[1] = v;
	dst.tc1[0] = u1; dst.tc1[1] = v1;
	dst.C[0] = col[0]; dst.C[1] = col[1]; dst.C[2] = col[2]; dst.C[3] = col[3];
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

// Shared 1x1 WHITE texture (in-memory BMP) for materials with NO base-color texture (factor-only, e.g.
// BrainStem's 59 flat-colored parts). The deferred ext PS computes albedo = s_base * ext_base_color, so
// binding WHITE makes albedo == baseColorFactor -- instead of multiplying by the dark missing-texture
// placeholder, which muddies every flat color. Created once + deduped by the texture pool. Returns the
// resource name, or NULL if it couldn't be created (DX9 / decode failure) so the caller uses the placeholder.
static const char* ext_white_texture_name()
{
	static const char* kName = "$user$gltf\\__white__";
	static int state = 0; // 0=untried, 1=created, 2=failed
	if (state == 0)
	{
		// minimal 1x1 24bpp BMP, single white pixel
		static const unsigned char bmp[58] = {
			'B', 'M', 0x3A, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0,             // BITMAPFILEHEADER (14)
			0x28, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, // BITMAPINFOHEADER ...
			4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // ... (40 bytes)
			0xFF, 0xFF, 0xFF, 0x00                                          // pixel BGR + pad (4)
		};
		ID3DBaseTexture* tex = ext_create_texture_from_memory(bmp, sizeof(bmp), "gltf_white");
		if (tex)
		{
			ref_texture rt;
			rt.create(kName);
			if (rt._get())
			{
				rt->surface_set(tex);
				state = 1;
			}
			else
				state = 2;
			_RELEASE(tex);
		}
		else
			state = 2;
	}
	return (state == 1) ? kName : NULL;
}

// Synthetic resource name for a model's decoded normal map (distinct namespace from the albedo).
static void ext_make_user_name_n(string_path out, const char* short_name)
{
	strconcat(sizeof(string_path), out, "$user$gltf_n\\", short_name ? short_name : "unnamed");
}

// Synthetic resource name for a model's decoded metallic-roughness map (its own namespace).
static void ext_make_user_name_mr(string_path out, const char* short_name)
{
	strconcat(sizeof(string_path), out, "$user$gltf_mr\\", short_name ? short_name : "unnamed");
}

// Synthetic resource name for a model's decoded emissive map (its own namespace).
static void ext_make_user_name_e(string_path out, const char* short_name)
{
	strconcat(sizeof(string_path), out, "$user$gltf_e\\", short_name ? short_name : "unnamed");
}

// Synthetic resource name for a model's decoded SEPARATE occlusion (AO) map (its own namespace).
static void ext_make_user_name_ao(string_path out, const char* short_name)
{
	strconcat(sizeof(string_path), out, "$user$gltf_ao\\", short_name ? short_name : "unnamed");
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
// External loader
//////////////////////////////////////////////////////////////////////

// glTF front-door conformance gate (Phase 1 -- kill silent corruption). Returns true (and logs ONE
// reason via Msg) when the parsed model uses something this loader cannot honor and would otherwise
// render as garbage with no diagnostic:
//  - a REQUIRED extension we don't implement (extensionsRequired)         -> 1.2
//  - Draco / meshopt-compressed geometry cgltf parses but never decodes   -> 1.3
//  - a sparse accessor on data we consume: cgltf_accessor_read_float/_uint/_index silently return 0 for
//    sparse accessors (cgltf.h:2357/2501/2521/2644), collapsing the mesh to the origin                -> 1.1
// The caller rejects the load on true. External-path only; stock OGF/OMF is unaffected.
static bool ext_gltf_unsupported(const cgltf_data* gltf, const char* full_path)
{
	// 1.2 -- extensionsRequired allowlist. Anything the asset *requires* that we don't implement -> reject.
	// (Extend this list as features land. KHR_texture_transform/mesh_quantization/emissive_strength are
	// the ones the loader honors today.)
	for (cgltf_size i = 0; i < gltf->extensions_required_count; ++i)
	{
		const char* ext = gltf->extensions_required[i];
		const bool ok =
			!xr_strcmp(ext, "KHR_materials_emissive_strength") ||
			!xr_strcmp(ext, "KHR_texture_transform") ||
			!xr_strcmp(ext, "KHR_mesh_quantization");
		if (!ok)
		{
			Msg("! [gltf] '%s' requires unsupported extension '%s' -- not loading", full_path, ext);
			return true;
		}
	}

	// 1.3 -- EXT_meshopt_compression on any bufferView (cgltf reads metadata, doesn't decompress).
	for (cgltf_size bi = 0; bi < gltf->buffer_views_count; ++bi)
		if (gltf->buffer_views[bi].has_meshopt_compression)
		{
			Msg("! [gltf] '%s' uses EXT_meshopt_compression (not decoded) -- not loading", full_path);
			return true;
		}

	// 1.3 Draco + 1.1 sparse on consumed vertex/index accessors, per primitive.
	for (cgltf_size mi = 0; mi < gltf->meshes_count; ++mi)
	{
		const cgltf_mesh& mesh = gltf->meshes[mi];
		for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
		{
			const cgltf_primitive& prim = mesh.primitives[pi];
			if (prim.has_draco_mesh_compression)
			{
				Msg("! [gltf] '%s' uses KHR_draco_mesh_compression (not decoded) -- not loading", full_path);
				return true;
			}
			if (prim.indices && prim.indices->is_sparse)
			{
				Msg("! [gltf] '%s' has a sparse index accessor (unsupported) -- not loading", full_path);
				return true;
			}
			for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
			{
				const cgltf_accessor* a = prim.attributes[ai].data;
				if (a && a->is_sparse)
				{
					Msg("! [gltf] '%s' has a sparse vertex accessor (%s) -- not loading", full_path,
						prim.attributes[ai].name ? prim.attributes[ai].name : "?");
					return true;
				}
			}
		}
	}

	// 1.1 -- sparse inverseBindMatrices (the skinned path reads this accessor directly).
	for (cgltf_size si = 0; si < gltf->skins_count; ++si)
	{
		const cgltf_accessor* ibm = gltf->skins[si].inverse_bind_matrices;
		if (ibm && ibm->is_sparse)
		{
			Msg("! [gltf] '%s' has a sparse inverseBindMatrices accessor (unsupported) -- not loading", full_path);
			return true;
		}
	}

	return false;
}

// 3.3 -- collect the nodes of the default scene, recursively (root nodes + their descendants).
// cgltf_node_transform_world() gives each node's world matrix regardless of how it's reached, so only
// the SET of visited nodes matters; recursing the default scene excludes nodes not in any scene. Falls
// back to the flat gltf->nodes[] list when the file declares no scenes.
static void ext_collect_node(const cgltf_node* node, xr_vector<const cgltf_node*>& out, int depth = 0)
{
	if (!node || depth > 256) return; // depth cap guards against a malformed cyclic node graph
	out.push_back(node);
	for (cgltf_size i = 0; i < node->children_count; ++i)
		ext_collect_node(node->children[i], out, depth + 1);
}
static void ext_scene_nodes(const cgltf_data* gltf, xr_vector<const cgltf_node*>& out)
{
	const cgltf_scene* scene = gltf->scene ? gltf->scene : (gltf->scenes_count ? &gltf->scenes[0] : NULL);
	if (scene)
		for (cgltf_size i = 0; i < scene->nodes_count; ++i)
			ext_collect_node(scene->nodes[i], out);
	else
		for (cgltf_size i = 0; i < gltf->nodes_count; ++i)
			out.push_back(&gltf->nodes[i]);
}

// 3.4 -- model-wide post-transform bounds (ALL scene primitives, ignoring the per-material filter), in the
// SAME final vertex space as the loaded geometry (node-world bake + Z-flip). Used so every per-material
// child computes ONE shared auto-scale factor instead of one per filtered subset (which tears multi-material
// models apart). Buffers must already be resolved.
static void ext_model_bounds(const cgltf_data* gltf, const xr_vector<const cgltf_node*>& nodes, Fbox& bb)
{
	bb.invalidate();
	for (const cgltf_node* np : nodes)
	{
		const cgltf_node& node = *np;
		if (!node.mesh) continue;
		cgltf_float w[16]; cgltf_node_transform_world(&node, w);
		Fmatrix Mw; memcpy(&Mw, w, sizeof(w));
		const cgltf_mesh& mesh = *node.mesh;
		for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
		{
			const cgltf_primitive& prim = mesh.primitives[pi];
			if (prim.type != cgltf_primitive_type_triangles) continue;
			const cgltf_accessor* a_pos = NULL;
			for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
				if (prim.attributes[ai].type == cgltf_attribute_type_position) { a_pos = prim.attributes[ai].data; break; }
			if (!a_pos) continue;
			for (cgltf_size i = 0; i < a_pos->count; ++i)
			{
				Fvector P; P.set(0, 0, 0);
				cgltf_accessor_read_float(a_pos, i, &P.x, 3);
				Fvector t; Mw.transform_tiny(t, P); P = t;
#if EXTERNAL_FLIP_Z
				P.z = -P.z;
#endif
				bb.modify(P);
			}
		}
	}
}

bool FExternalVisual::GetMaterialIndices(const char* full_path, xr_vector<MatInfo>& out)
{
	out.clear();

	IReader* rd = FS.r_open(full_path);
	if (!rd)
		return false;
	const size_t blob_size = (size_t)rd->length();
	void* blob = xr_malloc(blob_size);
	CopyMemory(blob, rd->pointer(), blob_size);
	FS.r_close(rd);

	// material assignments live in the JSON, so no cgltf_load_buffers() needed here
	cgltf_options options = {};
	cgltf_data* gltf = NULL;
	cgltf_result res = cgltf_parse(&options, blob, blob_size, &gltf);
	if (res != cgltf_result_success)
	{
		xr_free(blob);
		return false;
	}

	// distinct materials used by triangle primitives, in first-seen order (mirrors the node iteration
	// in LoadExternal so the set matches what actually emits geometry); record emissive presence too
	xr_vector<const cgltf_node*> scene_nodes;
	ext_scene_nodes(gltf, scene_nodes);
	for (const cgltf_node* _np : scene_nodes)
	{
		const cgltf_node& node = *_np;
		if (!node.mesh)
			continue;
		const cgltf_mesh& mesh = *node.mesh;
		for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
		{
			const cgltf_primitive& prim = mesh.primitives[pi];
			if (prim.type != cgltf_primitive_type_triangles)
				continue;
			const int idx = prim.material ? (int)(prim.material - gltf->materials) : -1;
			bool seen = false;
			for (const MatInfo& v : out) if (v.index == idx) { seen = true; break; }
			if (!seen)
			{
				const bool emis = prim.material && prim.material->emissive_texture.texture
					&& prim.material->emissive_texture.texture->image;
				// metallic = has a metallic-roughness map AND metallic_factor > 0 (cgltf defaults the
				// factor to 1.0 per spec). Drives the forward colored-reflection overlay. (Factor-only
				// metals with no MR map aren't handled yet -- they'd need a constant + white MR stand-in.)
				bool metal = false;
				if (prim.material && prim.material->has_pbr_metallic_roughness)
				{
					const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
					// metallicFactor defaults to 1.0 (the glTF default), so an untextured default-PBR
					// material reads as metal -- spec-correct. Factor-only metals (no MR texture) get a 1x1
					// white MR stand-in in LoadExternal so they render metallic instead of plastic. (3.1)
					metal = (pbr.metallic_factor > 0.01f);
				}
				// transparent surface (renders forward/blended instead of the deferred batch)
				const bool blend = prim.material && prim.material->alpha_mode == cgltf_alpha_mode_blend;
				out.push_back(MatInfo{ idx, emis, metal, blend });
			}
		}
	}

	cgltf_free(gltf);
	xr_free(blob);
	return true;
}

bool FExternalVisual::GetSkinData(const char* full_path, ExtSkinData& out)
{
	out.bones.clear();
	out.root = -1;

	IReader* rd = FS.r_open(full_path);
	if (!rd)
		return false;
	const size_t blob_size = (size_t)rd->length();
	void* blob = xr_malloc(blob_size);
	CopyMemory(blob, rd->pointer(), blob_size);
	FS.r_close(rd);

	cgltf_options options = {};
	cgltf_data* gltf = NULL;
	cgltf_result res = cgltf_parse(&options, blob, blob_size, &gltf);
	if (res != cgltf_result_success)
	{
		xr_free(blob);
		return false;
	}
	// inverseBindMatrices live in a buffer accessor -> buffers must be resolved
	res = cgltf_load_buffers(&options, gltf, full_path);
	if (res != cgltf_result_success)
	{
		cgltf_free(gltf);
		xr_free(blob);
		return false;
	}

	if (gltf->skins_count == 0)
	{
		cgltf_free(gltf); // not a skinned model -> caller falls back to the static/rigid path
		xr_free(blob);
		return false;
	}

	if (gltf->skins_count > 1) // finding F: only the first skin is built; make multi-skin models non-silent
		Msg("! [gltf] '%s' has %u skins; using the first (multi-skin not supported)", full_path, (u32)gltf->skins_count);
	const cgltf_skin& skin = gltf->skins[0]; // Phase A: first skin only
	const cgltf_size  n = skin.joints_count;
	if (n == 0)
	{
		cgltf_free(gltf);
		xr_free(blob);
		return false;
	}

	// glTF RH -> engine LH coordinate conversion (Z-flip), an involution. MUST match the conversion the
	// skinned geometry loader applies to its vertices, so bones and verts stay in the same space. (Model
	// orientation is carried by the joint transforms + IBMs -- no per-asset axis fixups needed.)
	Fmatrix C;
	C.identity();
	C._33 = -1.f;

	// Absolute world bind transform of each joint. glTF is column-major / column-vector, X-Ray is
	// row-major / row-vector; the two transposes cancel, so the 16 floats memcpy straight onto the
	// row-major Fmatrix and apply the same transform as p*M (exactly the static path's convention).
	xr_vector<Fmatrix> world;
	world.resize(n);
	for (cgltf_size i = 0; i < n; ++i)
	{
		cgltf_float w[16];
		cgltf_node_transform_world(skin.joints[i], w);
		CopyMemory(&world[i], w, sizeof(w));
		// Conjugate the world bind by C (the same conversion applied to the verts) so the skinning stays
		// consistent in engine space: C*M*C (C is an involution -> C^-1 == C).
		{
			Fmatrix t; t.mul_43(C, world[i]); world[i].mul_43(t, C);
		}
	}

	const cgltf_accessor* ibm = skin.inverse_bind_matrices; // optional (model->bone), for cross-check

	out.bones.resize(n);
	int root_count = 0;
	for (cgltf_size i = 0; i < n; ++i)
	{
		ExtBone& b = out.bones[i];
		const cgltf_node* jn = skin.joints[i];
		b.gltf_node = (int)(jn - gltf->nodes);
		if (jn->name && jn->name[0])
			b.name = jn->name;
		else
		{
			string64 nm;
			xr_sprintf(nm, "$bone_%u$", (u32)i);
			b.name = nm;
		}

		// parent BONE = nearest ancestor node that is also a joint of THIS skin (walk node->parent up)
		int parent_bone = -1;
		for (const cgltf_node* p = jn->parent; p; p = p->parent)
		{
			for (cgltf_size j = 0; j < n; ++j)
				if (skin.joints[j] == p) { parent_bone = (int)j; break; }
			if (parent_bone >= 0)
				break;
		}
		b.parent = parent_bone;
		if (parent_bone < 0)
		{
			++root_count;
			out.root = (int)i;
		}

		// local bind: world_i = parent_world * bind_local  =>  bind_local = inv(parent_world) * world_i
		if (parent_bone < 0)
			b.bind_local = world[i];
		else
		{
			Fmatrix invP;
			invP.invert(world[parent_bone]);
			b.bind_local.mul_43(invP, world[i]);
		}

		// glTF inverseBindMatrix (model->bone). This is AUTHORITATIVE and is used directly as the bone's
		// m2b: it may encode a skeleton-root / bind-shape offset that inverse(joint world) does NOT (e.g.
		// a skin whose IBMs are relative to a skeleton root under an oriented armature -- CesiumMan). The
		// hierarchy inverse would mis-orient/deform such skins.
		if (ibm && i < ibm->count)
		{
			cgltf_float m[16];
			cgltf_accessor_read_float(ibm, i, m, 16);
			CopyMemory(&b.inv_bind, m, sizeof(m));
			// IBM is raw glTF space; conjugate by C (m2b^engine = C * IBM * C) to match the converted verts
			// + conjugated world bind, so mRenderTransform stays consistent in engine space.
			{
				Fmatrix t; t.mul_43(C, b.inv_bind); b.inv_bind.mul_43(t, C);
			}
		}
		else
			b.inv_bind.invert(world[i]); // no IBM: fall back to inverse(world bind); world[i] already conjugated
	}

	// X-Ray needs exactly one root. We ALWAYS append a synthetic IDENTITY root and reparent every joint
	// root to it (4.5). This guarantees the skeleton root is identity, so the model-space collision/OBB box
	// that FExternalKinematics puts on the root bone isn't offset by a non-identity skin root (e.g.
	// CesiumMan's armature, whose root has a baked orientation). bind_local of a root was its world
	// (parent=identity in glTF), which stays correct under an identity parent -> the bind pose / rendering
	// is unchanged. JOINTS_0 never indexes the appended bone (it is not a skin joint).
	{
		const int synth_id = (int)out.bones.size();
		for (cgltf_size i = 0; i < n; ++i)
			if (out.bones[i].parent < 0)
				out.bones[i].parent = synth_id;
		ExtBone synth;
		synth.name = "$external_skel_root$";
		synth.parent = -1;
		synth.gltf_node = -1;
		synth.bind_local.identity();
		synth.inv_bind.identity();
		out.bones.push_back(synth);
		out.root = synth_id;
	}

	Msg("* [gltf] skin '%s': %u joints (+1 synth identity root), root=%d", full_path, (u32)n, out.root);

	cgltf_free(gltf);
	xr_free(blob);
	return true;
}

bool FExternalVisual::LoadExternal(const char* short_name, const char* full_path, int material_filter, EExtPass pass)
{
	dbg_name = short_name;
	dbg_id = 1;
	skinning = -1; // SKIN_NONE: m_skinning < 0 selects the static v_model VS path (r4.cpp:1487)
	hud = false;

	// Overlay-pass flags (kept as bools so the rest of the loader reads naturally).
	const bool emissive_pass = (pass == ext_emissive);
	const bool metal_pass    = (pass == ext_metal);
	const bool blend_pass    = (pass == ext_blend);

	// Per-material key so each material child's decoded textures get a unique $user$ name (no
	// collisions between submeshes of the same model). -1 (merge-all) keeps the bare name.
	string_path tex_key;
	if (material_filter != -1)
		xr_sprintf(tex_key, "%s_m%d", short_name, material_filter);
	else
		xr_strcpy(tex_key, sizeof(tex_key), short_name);

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

	// Phase 1 front-door gate: reject (with a log) anything we'd otherwise render as garbage --
	// required-but-unimplemented extensions, Draco/meshopt geometry, or sparse accessors.
	if (ext_gltf_unsupported(gltf, full_path))
	{
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
	const cgltf_image* emissive_img = NULL;
	const cgltf_image* occ_img = NULL;  // glTF occlusionTexture image (AO); R channel = ambient occlusion
	float occ_strength = 1.f;           // occlusionTexture.strength (cgltf stores it in .scale)
	Fvector emissive_scale;
	emissive_scale.set(1.f, 1.f, 1.f); // glTF emissiveFactor * emissive_strength (captured with the map)
	cgltf_alpha_mode alpha_mode = cgltf_alpha_mode_opaque; // glTF alphaMode of this child's material
	float            alpha_cutoff = 0.5f;                  // glTF alphaCutoff (default 0.5)
	float            base_alpha = 1.f;                     // glTF baseColorFactor.a (opacity for BLEND)
	bool             alpha_captured = false;
	float base_color_factor[3] = {1.f, 1.f, 1.f};         // glTF baseColorFactor.rgb (albedo/F0 tint)
	float metallic_factor = 1.f, roughness_factor = 1.f, normal_scale = 1.f; // glTF scalar factors
	bool  mat_has_pbr = false;                                              // material declares pbrMetallicRoughness (3.1)
	float uv_scale[2] = {1.f, 1.f}, uv_offset[2] = {0.f, 0.f};               // KHR_texture_transform
	float uv_rot = 0.f;                                                      // KHR_texture_transform rotation (radians)
	int   uv_set[4] = {0, 0, 0, 0};                                         // per-map texCoord: base/normal/mr/ao (0=UV0,1=UV1) (3.5)
	bool  double_sided = false;                                            // glTF material doubleSided (3.2)
	bool  have_vertex_color = false;                       // any primitive carries glTF COLOR_0

	// Iterate the scene graph so node transforms (translate/rotate/scale) are baked into the
	// geometry. Real exported models position meshes via nodes; ignoring them mis-places/scales
	// the mesh. (A glTF with meshes but no node referencing them is unusual and emits nothing.)
	xr_vector<const cgltf_node*> scene_nodes;
	ext_scene_nodes(gltf, scene_nodes);
	for (const cgltf_node* _np : scene_nodes)
	{
		const cgltf_node& node = *_np;
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

			// multi-material filter. material_filter: -1 = take all primitives; >=0 = only that
			// material index; -2 = only primitives with NO material (so a mixed model's no-material
			// group is a distinct child rather than re-merging everything).
			const int prim_mat = prim.material ? (int)(prim.material - gltf->materials) : -1;
			if (material_filter == -2)
			{
				if (prim_mat != -1) continue;
			}
			else if (material_filter >= 0)
			{
				if (prim_mat != material_filter) continue;
			}

			// locate attributes
			const cgltf_accessor* a_pos = NULL;
			const cgltf_accessor* a_nrm = NULL;
			const cgltf_accessor* a_uv = NULL;
			const cgltf_accessor* a_uv1 = NULL; // glTF TEXCOORD_1 (2nd UV set; 3.5 per-map routing)
			const cgltf_accessor* a_tan = NULL;
			const cgltf_accessor* a_col = NULL;
			for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
			{
				const cgltf_attribute& at = prim.attributes[ai];
				switch (at.type)
				{
				case cgltf_attribute_type_position: a_pos = at.data; break;
				case cgltf_attribute_type_normal: a_nrm = at.data; break;
				case cgltf_attribute_type_texcoord:
					if      (at.index == 0) a_uv = at.data;
					else if (at.index == 1) a_uv1 = at.data;
					break;
				case cgltf_attribute_type_tangent: a_tan = at.data; break;
				case cgltf_attribute_type_color: if (at.index == 0) a_col = at.data; break;
				default: break;
				}
			}
			if (!a_pos)
				continue;
			if (a_col)
				have_vertex_color = true;

			const u32 v_base = (u32)verts.size();
			const cgltf_size v_count = a_pos->count;

			// 4.1 -- generate tangents when this primitive has a normal map but NO TANGENT attribute (else
			// T stays the constant (1,0,0) -> mis-lit bumps). Per-triangle UV-gradient accumulation
			// (MikkTSpace-lite), computed in RAW glTF space; each vertex's tangent is orthonormalized
			// against its normal and baked/Z-flipped with the rest in the loop below.
			xr_vector<Fvector> gen_tan;
			const bool need_tangents = !a_tan && a_uv && a_nrm
				&& prim.material && prim.material->normal_texture.texture;
			if (need_tangents)
			{
				Fvector zero; zero.set(0, 0, 0);
				gen_tan.assign(v_count, zero);
				const cgltf_size n = prim.indices ? prim.indices->count : v_count;
				for (cgltf_size t = 0; t + 3 <= n; t += 3)
				{
					const cgltf_size i0 = prim.indices ? cgltf_accessor_read_index(prim.indices, t + 0) : t + 0;
					const cgltf_size i1 = prim.indices ? cgltf_accessor_read_index(prim.indices, t + 1) : t + 1;
					const cgltf_size i2 = prim.indices ? cgltf_accessor_read_index(prim.indices, t + 2) : t + 2;
					if (i0 >= v_count || i1 >= v_count || i2 >= v_count) continue;
					Fvector p0, p1, p2; float t0[2], t1[2], t2[2];
					cgltf_accessor_read_float(a_pos, i0, &p0.x, 3);
					cgltf_accessor_read_float(a_pos, i1, &p1.x, 3);
					cgltf_accessor_read_float(a_pos, i2, &p2.x, 3);
					cgltf_accessor_read_float(a_uv, i0, t0, 2);
					cgltf_accessor_read_float(a_uv, i1, t1, 2);
					cgltf_accessor_read_float(a_uv, i2, t2, 2);
					Fvector e1, e2; e1.sub(p1, p0); e2.sub(p2, p0);
					const float du1 = t1[0] - t0[0], dv1 = t1[1] - t0[1];
					const float du2 = t2[0] - t0[0], dv2 = t2[1] - t0[1];
					const float det = du1 * dv2 - du2 * dv1;
					if (_abs(det) < 1e-12f) continue; // degenerate UVs -> skip this triangle's contribution
					const float r = 1.f / det;
					Fvector tg;
					tg.x = (e1.x * dv2 - e2.x * dv1) * r;
					tg.y = (e1.y * dv2 - e2.y * dv1) * r;
					tg.z = (e1.z * dv2 - e2.z * dv1) * r;
					gen_tan[i0].add(tg); gen_tan[i1].add(tg); gen_tan[i2].add(tg);
				}
			}

			verts.reserve(verts.size() + v_count);
			bool read_warned = false;
			for (cgltf_size i = 0; i < v_count; ++i)
			{
				Fvector P, N, T;
				P.set(0, 0, 0);
				N.set(0, 1, 0); // default up-normal if the mesh has no normals
				T.set(1, 0, 0);
				float uv[2] = {0, 0};
				float uv1[2] = {0, 0};
				float tan4[4] = {1, 0, 0, 1};
				float col[4] = {1, 1, 1, 1}; // glTF COLOR_0 (default opaque white)

				// 1.1 -- read_float returns false (output untouched) for sparse/corrupt accessors; sparse is
				// already gated out, so this guards the residual corrupt-buffer case. Log once, keep default.
				if (!cgltf_accessor_read_float(a_pos, i, &P.x, 3) && !read_warned)
				{
					Msg("! [gltf] '%s' POSITION read failed (corrupt accessor)", full_path);
					read_warned = true;
				}
				if (a_nrm) cgltf_accessor_read_float(a_nrm, i, &N.x, 3);
				if (a_uv) cgltf_accessor_read_float(a_uv, i, uv, 2);
				if (a_uv1) cgltf_accessor_read_float(a_uv1, i, uv1, 2);
				else { uv1[0] = uv[0]; uv1[1] = uv[1]; } // no 2nd UV set -> mirror UV0 (maps routed to UV1 still work)
				if (a_tan)
				{
					cgltf_accessor_read_float(a_tan, i, tan4, 4);
					T.set(tan4[0], tan4[1], tan4[2]);
				}
				else if (need_tangents)
				{
					// 4.1 -- Gram-Schmidt orthonormalize the generated tangent against the raw normal
					Fvector nn = N; nn.normalize_safe();
					Fvector tg = gen_tan[i];
					const float d = nn.dotproduct(tg);
					tg.x -= nn.x * d; tg.y -= nn.y * d; tg.z -= nn.z * d;
					if (tg.magnitude() > 1e-5f) { tg.normalize(); T = tg; } // else keep default (1,0,0)
				}
				if (a_col) // COLOR_0 is VEC3 or VEC4; read the right count so alpha stays 1 for VEC3
					cgltf_accessor_read_float(a_col, i, col, (a_col->type == cgltf_type_vec4) ? 4 : 3);

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
				ext_set_vertex(vx, P, N, T, B, uv[0], uv[1], uv1[0], uv1[1], col);
				verts.push_back(vx);
				bb.modify(P);
			}

			// indices (offset by v_base), per triangle, with optional winding reversal to match
			// the engine's left-handed front-face convention (see EXTERNAL_FLIP_WINDING note).
			{
				const cgltf_size n = prim.indices ? prim.indices->count : v_count;
				indices.reserve(indices.size() + n);
				bool idx_warned = false;
				for (cgltf_size i = 0; i + 3 <= n; i += 3)
				{
					const cgltf_size ia = prim.indices ? cgltf_accessor_read_index(prim.indices, i + 0) : i + 0;
					const cgltf_size ib = prim.indices ? cgltf_accessor_read_index(prim.indices, i + 1) : i + 1;
					const cgltf_size ic = prim.indices ? cgltf_accessor_read_index(prim.indices, i + 2) : i + 2;
					// 1.4 index sanity: indices are local to this primitive's vertex array; an out-of-range
					// index (corrupt asset) would read past the VB. Skip the triangle, log once per primitive.
					if (ia >= v_count || ib >= v_count || ic >= v_count)
					{
						if (!idx_warned)
						{
							Msg("! [gltf] '%s' index >= vertex count (%u) -- skipping triangle(s)", full_path, (u32)v_count);
							idx_warned = true;
						}
						continue;
					}
					const u32 a = (u32)(v_base + ia);
					const u32 b = (u32)(v_base + ib);
					const u32 c = (u32)(v_base + ic);
#if EXTERNAL_FLIP_WINDING
					indices.push_back(a); indices.push_back(c); indices.push_back(b);
#else
					indices.push_back(a); indices.push_back(b); indices.push_back(c);
#endif
				}
			}

			// glTF alphaMode/cutoff of this child's material (first one in the filtered primitive set)
			if (!alpha_captured && prim.material)
			{
				const cgltf_material* m = prim.material;
				alpha_mode = m->alpha_mode;
				alpha_cutoff = m->alpha_cutoff;
				double_sided = m->double_sided; // glTF doubleSided -> no-cull shader variant (3.2)
				if (m->has_pbr_metallic_roughness)
				{
					const cgltf_pbr_metallic_roughness& pbr = m->pbr_metallic_roughness;
					base_color_factor[0] = pbr.base_color_factor[0];
					base_color_factor[1] = pbr.base_color_factor[1];
					base_color_factor[2] = pbr.base_color_factor[2];
					base_alpha = pbr.base_color_factor[3];
					metallic_factor = pbr.metallic_factor;
					roughness_factor = pbr.roughness_factor;
					mat_has_pbr = true; // for factor-only metal detection (3.1)
					uv_set[0] = pbr.base_color_texture.texcoord;         // 3.5 per-map texCoord
					uv_set[2] = pbr.metallic_roughness_texture.texcoord;
					// KHR_texture_transform (offset+scale+rotation) from the base-color texture, applied to all maps
					if (pbr.base_color_texture.has_transform)
					{
						const cgltf_texture_transform& tt = pbr.base_color_texture.transform;
						uv_scale[0] = tt.scale[0];   uv_scale[1] = tt.scale[1];
						uv_offset[0] = tt.offset[0]; uv_offset[1] = tt.offset[1];
						uv_rot = tt.rotation; // radians; converted to cos/sin in the member copy below
					}
				}
				if (m->normal_texture.texture) { normal_scale = m->normal_texture.scale; uv_set[1] = m->normal_texture.texcoord; } // glTF normalScale + texCoord
				uv_set[3] = m->occlusion_texture.texcoord; // 3.5 AO texCoord
				alpha_captured = true;
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

			// remember the first material's occlusion (AO) map + strength. cgltf stores the glTF
			// occlusionTexture.strength in the texture_view's .scale field.
			if (!occ_img && prim.material && prim.material->occlusion_texture.texture)
			{
				const cgltf_texture* ot = prim.material->occlusion_texture.texture;
				if (ot->image) { occ_img = ot->image; occ_strength = prim.material->occlusion_texture.scale; }
			}

			// remember the first material's emissive map + its factor*strength (-> additive overlay)
			if (!emissive_img && prim.material && prim.material->emissive_texture.texture)
			{
				const cgltf_texture* et = prim.material->emissive_texture.texture;
				if (et->image)
				{
					emissive_img = et->image;
					const cgltf_material* m = prim.material;
					const float str = m->has_emissive_strength ? m->emissive_strength.emissive_strength : 1.f;
					emissive_scale.set(m->emissive_factor[0] * str, m->emissive_factor[1] * str, m->emissive_factor[2] * str);
				}
			}
		}
	}

	// Resolve the base-color image into a GPU texture BEFORE freeing gltf -- the image bytes live in
	// gltf-owned buffer memory. Also copy the URI-derived name for the pre-converted-.dds fallback.
	string_path tex_name;
	bool have_tex = ext_texture_name_from_uri(base_tex_uri, tex_name);
	// Decode just what THIS pass uses (avoids wasted decodes on the doubled overlay children):
	//   ext_lit   -> albedo + normal + metal-rough(+AO)   ext_emissive -> emissive map only
	//   ext_metal -> albedo + metal-rough (no normal)     ext_blend    -> albedo only
	ID3DBaseTexture* decoded_tex = (!emissive_pass && base_img) ? ext_decode_gltf_image(base_img, full_path) : NULL;
	ID3DBaseTexture* decoded_nrm = (!emissive_pass && !metal_pass && !blend_pass && normal_img) ? ext_decode_gltf_image(normal_img, full_path) : NULL;
	ID3DBaseTexture* decoded_mr = (!emissive_pass && !blend_pass && mr_img) ? ext_decode_gltf_image(mr_img, full_path) : NULL;
	ID3DBaseTexture* decoded_emis = (emissive_pass && emissive_img) ? ext_decode_gltf_image(emissive_img, full_path) : NULL;
	// separate occlusion (AO) texture -- decode only when it's a DIFFERENT image than the MR map (ORM
	// occlusion lives in the MR texture's R, so it needs no separate decode). Lit pass only.
	ID3DBaseTexture* decoded_ao = (!emissive_pass && !metal_pass && !blend_pass && occ_img && occ_img != mr_img)
		? ext_decode_gltf_image(occ_img, full_path) : NULL;

	// 3.4 -- compute the model-wide bounds BEFORE cgltf_free (ext_model_bounds reads node/accessor data;
	// reading it after the free would be a use-after-free).
#if EXTERNAL_AUTOSCALE
	Fbox model_bb; ext_model_bounds(gltf, scene_nodes, model_bb);
#endif

	cgltf_free(gltf);
	xr_free(blob);

	if (verts.empty() || indices.empty())
	{
		Msg("! [gltf] '%s' has no triangle geometry", full_path);
		return false;
	}

	// --- auto-scale only ABSURDLY-sized models (3.4) ---------------------------------
	// glTF is authored in metres, so honour real scale by default. Only rescale assets whose largest
	// dimension is genuinely absurd (>50u or <0.01u). The factor is computed from the WHOLE model's bounds
	// (ext_model_bounds, all scene primitives) -- NOT this child's filtered subset -- so every per-material
	// child gets the SAME factor and multi-material models don't tear apart. Uniform scale about the local
	// origin keeps the bbox + physics box consistent.
#if EXTERNAL_AUTOSCALE
	{
		float maxdim = model_bb.max.x - model_bb.min.x;
		const float dy = model_bb.max.y - model_bb.min.y;
		const float dz = model_bb.max.z - model_bb.min.z;
		if (dy > maxdim) maxdim = dy;
		if (dz > maxdim) maxdim = dz;

		float s = 1.0f;
		if (maxdim > EXTERNAL_AUTOSCALE_MAX_SIZE)
			s = EXTERNAL_AUTOSCALE_MAX_SIZE / maxdim;                    // absurdly large -> shrink
		else if (maxdim > 1e-4f && maxdim < EXTERNAL_AUTOSCALE_MIN_SIZE)
			s = EXTERNAL_AUTOSCALE_MIN_SIZE / maxdim;                    // absurdly tiny -> grow

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
			Msg("~ [gltf] '%s' auto-scaled x%.4f (model dim %.2f -> %.2f units)", full_path, s, maxdim, maxdim * s);
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

	// Shared per-material factors + texture transform (used by every lit/metal/blend child's shader via
	// external_common.h). Identity defaults mean materials that set none of these render unchanged.
	m_base_color.set(base_color_factor[0], base_color_factor[1], base_color_factor[2]);
	m_mr_factor.set(metallic_factor, roughness_factor, normal_scale);
	m_uv_xform.set(uv_scale[0], uv_scale[1], uv_offset[0], uv_offset[1]);
	m_uv_rot.set(cosf(uv_rot), sinf(uv_rot)); // KHR_texture_transform rotation (radians -> cos/sin)
	m_base_alpha = base_alpha;                // glTF baseColorFactor.a (multiplies texel alpha before MASK clip)
	m_uv_set.set((float)uv_set[0], (float)uv_set[1], (float)uv_set[2], (float)uv_set[3]); // 3.5 per-map texCoord

	// --- emissive overlay pass ------------------------------------------------------
	// Forward additive batch: bind only the emissive map and the external_emissive shader. If the
	// selected material has no emissive map this child is pointless, so fail (FExternalKinematics
	// then skips it; the GPU buffers built above are released by the dtor).
	if (emissive_pass)
	{
		if (!decoded_emis)
			return false;
		ref_texture user_emis; // must outlive SetShaderTexture
		string_path emis_name;
		ext_make_user_name_e(emis_name, tex_key);
		user_emis.create(emis_name);
		if (user_emis._get())
			user_emis->surface_set(decoded_emis);
		_RELEASE(decoded_emis);
		SetShaderTexture(EXTERNAL_EMISSIVE_SHADER, emis_name);
		m_emissive = true;
		m_emissive_scale = emissive_scale; // glTF emissiveFactor * emissive_strength
		Type = MT_EXTERNAL_STATIC;
		return true;
	}

	// --- metal-reflection overlay pass ----------------------------------------------
	// Forward additive batch: bind albedo (reflection tint = F0) + the metallic-roughness map (metal
	// mask) and the external_metal shader. Needs both maps; if either is missing this child is pointless
	// so fail (FExternalKinematics skips it; the GPU buffers built above are released by the dtor). Uses
	// a metal-specific $user$ key so its decoded textures don't collide with the lit child's same-name
	// albedo/MR (both children parse the same material independently).
	if (metal_pass)
	{
		// 3.1: factor-only metals (metallicFactor>0, no MR texture) use a 1x1 white MR stand-in for the
		// reflection mask, as long as there's an albedo to tint the reflection. Textureless factor-only
		// metals skip the overlay -- their lit MR child still carries the metallic look.
		const bool white_mr = !decoded_mr && mat_has_pbr && !mr_img && metallic_factor > 0.01f;
		if (!decoded_tex || (!decoded_mr && !white_mr))
		{
			_RELEASE(decoded_tex);
			_RELEASE(decoded_mr);
			return false;
		}
		string_path mkey;
		xr_sprintf(mkey, "%s_metal", tex_key);

		ref_texture user_tex_m, user_mr_m; // must outlive SetShaderTexture
		string_path albedo_name, mrm_name;
		ext_make_user_name(albedo_name, mkey);
		user_tex_m.create(albedo_name);
		if (user_tex_m._get())
			user_tex_m->surface_set(decoded_tex);
		_RELEASE(decoded_tex);

		const char* mr_bind;
		if (decoded_mr)
		{
			ext_make_user_name_mr(mrm_name, mkey);
			user_mr_m.create(mrm_name);
			if (user_mr_m._get())
				user_mr_m->surface_set(decoded_mr);
			mr_bind = mrm_name;
			_RELEASE(decoded_mr);
		}
		else // factor-only metal: 1x1 white MR stand-in (constant metalness from the factor)
		{
			const char* w = ext_white_texture_name();
			if (!w) return false; // no mask available -> skip the overlay (lit MR still metallic)
			mr_bind = w;
		}

		string_path tlist_m;
		strconcat(sizeof(tlist_m), tlist_m, albedo_name, ",", mr_bind); // albedo + metal-rough
		SetShaderTexture(EXTERNAL_METAL_SHADER, tlist_m);
		m_metal = true;
		Type = MT_EXTERNAL_STATIC;
		return true;
	}

	// glTF alphaMode for this LIT batch: MASK clips at the material's alphaCutoff; OPAQUE (and, for now,
	// BLEND) use -1 so the deferred PS never clips. Render() pushes this as "ext_alpha_cutoff".
	m_alpha_cutoff = (alpha_mode == cgltf_alpha_mode_mask) ? alpha_cutoff : -1.f;

	// glTF occlusion strength (the MR deferred shaders read AO from s_ao.r). 0 = no occlusion map. s_ao is
	// bound below to the MR texture (ORM: occlusion in MR.r) or to the separately-decoded AO texture.
	m_ao_strength = occ_img ? occ_strength : 0.f;

	// --- material / shader ----------------------------------------------------------
	// Preferred: the runtime texture decoded from the glTF's embedded/external base-color image,
	// registered under a "$user$" name so the engine binds it without touching disk. Fallbacks, in
	// order: a pre-converted .dds already in $game_textures$, then the engine missing-texture
	// placeholder (X-Ray FATALS on a missing texture, so the bound name must always resolve).
	ref_texture user_tex; // must outlive SetShaderTexture so the shader can take its own ref
	ref_texture user_nrm; // ditto, for the normal map
	ref_texture user_mr;  // ditto, for the metallic-roughness map
	ref_texture user_ao;  // ditto, for a SEPARATE occlusion (AO) map
	bool resolved = false;

	if (decoded_tex)
	{
		ext_make_user_name(tex_name, tex_key);
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
		{
			// a texture was intended but didn't resolve -> visible placeholder (signals the error)
			Msg("~ [gltf] base texture '%s' not found; using placeholder for '%s'", tex_name, full_path);
			xr_strcpy(tex_name, sizeof(tex_name), EXTERNAL_FALLBACK_TEXTURE);
		}
		else
		{
			// factor-only material (no base texture): bind WHITE so albedo == baseColorFactor (not the
			// dark missing-texture placeholder). Falls back to the placeholder if white can't be created.
			const char* w = ext_white_texture_name();
			xr_strcpy(tex_name, sizeof(tex_name), w ? w : EXTERNAL_FALLBACK_TEXTURE);
		}
	}

	// --- forward BLEND surface -----------------------------------------------------
	// alphaMode=BLEND can't write the deferred G-buffer, so this child renders forward/alpha-blended with
	// just the albedo (opacity in .a). The albedo is already resolved above; bind it + external_blend and
	// stop here (no normal/MR/AO -- the forward pass is a simple lit blend).
	if (blend_pass)
	{
		SetShaderTexture(EXTERNAL_BLEND_SHADER, tex_name);
		m_blend = true;
		m_blend_alpha = base_alpha; // glTF baseColorFactor.a (overall opacity multiplier)
		Type = MT_EXTERNAL_STATIC;
		return true;
	}

	// Decode the normal map into its own $user$ texture. When present, switch to the bump shader and
	// hand it a "albedo,normal" texture list (-> t_base / t_second). glTF normal maps are linear, so
	// the default (non-sRGB) decode is correct here.
	string_path normal_name;
	bool have_normal = false;
	if (decoded_nrm)
	{
		ext_make_user_name_n(normal_name, tex_key);
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
		ext_make_user_name_mr(mr_name, tex_key);
		user_mr.create(mr_name);
		if (user_mr._get())
		{
			user_mr->surface_set(decoded_mr);
			have_mr = true;
		}
		_RELEASE(decoded_mr);
	}

	// 3.1 factor-only metals: a pbr material with metallicFactor > 0 but NO metallic-roughness texture.
	// Bind a 1x1 WHITE MR stand-in so the deferred MR shader reads metallic = white.B * metallicFactor and
	// roughness = white.G * roughnessFactor -- i.e. constant metalness from the factor -- instead of
	// falling to the plastic flat shader. ext_mr_factor (pushed in Render) carries the actual factors.
	if (!have_mr && mat_has_pbr && !mr_img && metallic_factor > 0.01f)
	{
		const char* w = ext_white_texture_name();
		if (w) { xr_strcpy(mr_name, sizeof(mr_name), w); have_mr = true; }
	}

	// SEPARATE occlusion map -> its own $user$ texture (linear decode). Only decoded when occlusion is a
	// DIFFERENT image than the MR map; ORM occlusion is read from the MR texture's R instead.
	bool have_sep_ao = false;
	string_path ao_sep_name;
	if (decoded_ao)
	{
		ext_make_user_name_ao(ao_sep_name, tex_key);
		user_ao.create(ao_sep_name);
		if (user_ao._get())
		{
			user_ao->surface_set(decoded_ao);
			have_sep_ao = true;
		}
		_RELEASE(decoded_ao);
	}

	// s_ao source for the MR shaders: the separate AO texture if we have one, else the MR texture (ORM
	// occlusion lives in MR.r; when there's no occlusion, ext_ao_strength is 0 so this bind is ignored).
	const char* ao_name = have_sep_ao ? ao_sep_name : mr_name;

	string_path tlist;
	if (have_normal && have_mr)
	{
		// albedo,normal,metalrough,ao -> t_base / t_second / t_metalrough / t_ao (engine forwards 3rd+4th)
		xr_sprintf(tlist, "%s,%s,%s,%s", tex_name, normal_name, mr_name, ao_name);
		SetShaderTexture(double_sided ? EXTERNAL_BUMP_MR_DS_SHADER : EXTERNAL_BUMP_MR_SHADER, tlist);
	}
	else if (have_normal)
	{
		strconcat(sizeof(tlist), tlist, tex_name, ",", normal_name);
		SetShaderTexture(double_sided ? EXTERNAL_BUMP_DS_SHADER : EXTERNAL_BUMP_SHADER, tlist);
	}
	else if (have_mr)
	{
		// albedo,metalrough,ao (no normal) -> t_base / t_second / t_ao
		xr_sprintf(tlist, "%s,%s,%s", tex_name, mr_name, ao_name);
		SetShaderTexture(double_sided ? EXTERNAL_MR_DS_SHADER : EXTERNAL_MR_SHADER, tlist);
	}
	else if (have_vertex_color)
	{
		// per-vertex colours, no normal/MR/base texture (e.g. BoxVertexColors): vertex colour is albedo.
		// (No doubleSided variant for the vertex-colour path -- rare; renders single-sided.)
		SetShaderTexture(EXTERNAL_VC_SHADER, tex_name);
	}
	else
	{
		SetShaderTexture(double_sided ? EXTERNAL_DEFAULT_DS_SHADER : EXTERNAL_DEFAULT_SHADER, tex_name);
	}

	Type = MT_EXTERNAL_STATIC;
	return true;
}

//////////////////////////////////////////////////////////////////////
// FExternalSkinned : skinned (vertBoned4W) external visual
//////////////////////////////////////////////////////////////////////

void FExternalSkinned::Load(const char* N, IReader* /*data*/, u32 /*dwFlags*/)
{
	// External skinned visuals are built by LoadExternal(), never through the OGF path.
	Msg("! FExternalSkinned::Load() called unexpectedly for [%s] - built via LoadExternal()", N);
}

void FExternalSkinned::Render(float LOD)
{
	PROF_EVENT("FExternalSkinned::Render");
	// Ensure the bones are evaluated before the sbones_array upload. A spawned object is a CLONE with
	// fresh, UNcalculated bone instances, and nothing re-runs CalculateBones for a rigid skeleton -> the
	// skinning would read identity transforms and the mesh would render in raw (un-posed, Z-up) vertex
	// space (== lying down). Forcing it here puts the skeleton in its bind/animated pose every frame.
	if (Parent)
		Parent->CalculateBones(TRUE);

	// The deferred ext flat PS (external_common.h) multiplies albedo by these constants. The skinned
	// child renders through the inherited path (which doesn't set them), so set safe Phase-A values here:
	// captured base-color tint + identity UV/MR, no alpha clip (opaque), no AO. Without this the shader
	// reads stale registers -> black / order-dependent output.
	RCache.set_c("ext_uv_transform", 1.f, 1.f, 0.f, 0.f);
	RCache.set_c("ext_uv_rot", 1.f, 0.f, 0.f, 0.f); // identity rotation (no KHR_texture_transform on skinned yet)
	RCache.set_c("ext_base_color", m_base_color.x, m_base_color.y, m_base_color.z, m_base_alpha); // .w = MASK alpha (4.3)
	RCache.set_c("ext_mr_factor", 1.f, 1.f, 1.f, 1.f);
	RCache.set_c("ext_alpha_cutoff", m_alpha_cutoff, 0.f, 0.f, 0.f); // glTF MASK cutoff (-1 = no clip) (4.3)
	RCache.set_c("ext_ao_strength", 0.f, 0.f, 0.f, 0.f);
	CSkeletonX_ST::Render(LOD); // sbones_array upload + draw
}

void FExternalSkinned::Copy(dxRender_Visual* pFrom)
{
	CSkeletonX_ST::Copy(pFrom); // base-class skin data (bones, HW buffers, shader)
	// CSkeletonX_ST::Copy doesn't know about our subclass material members, so copy them here -- else a
	// spawned/cloned skinned model loses its baseColorFactor tint + MASK cutoff.
	FExternalSkinned* src = (FExternalSkinned*)pFrom;
	m_base_color   = src->m_base_color;
	m_alpha_cutoff = src->m_alpha_cutoff;
	m_base_alpha   = src->m_base_alpha;
}

void FExternalSkinned::CalcPoseBBox(Fbox& bb)
{
	bb.invalidate();
	if (!Parent)
		return;
	const vertBoned4W* V = *Vertices4W; // CPU vertex backup built by _Load_hw
	if (!V)
		return;
	for (u32 i = 0; i < vCount; ++i)
	{
		const vertBoned4W& v = V[i];
		const float w3 = 1.f - v.w[0] - v.w[1] - v.w[2];
		const float w[4] = {v.w[0], v.w[1], v.w[2], w3};
		Fvector p;
		p.set(0.f, 0.f, 0.f);
		for (int k = 0; k < 4; ++k)
		{
			if (w[k] == 0.f)
				continue;
			Fvector t;
			Parent->LL_GetTransform_R(v.m[k]).transform_tiny(t, v.P); // bone render transform * vertex
			p.x += t.x * w[k];
			p.y += t.y * w[k];
			p.z += t.z * w[k];
		}
		bb.modify(p);
	}
}

bool FExternalSkinned::LoadExternal(const char* short_name, const char* full_path, int material_filter,
                                    const FExternalVisual::ExtSkinData& skin)
{
	dbg_name = short_name;
	dbg_id = 1;
	hud = false;
	skinning = 4; // SKIN_4: dxRender_Visual::SetShaderTexture pushes this to SetSkinningMode at shader-compile,
	              // so the model VS compiles as the 4-bone skinned variant (reads sbones_array). (Static sets -1.)

	// per-material $user$ texture key (matches FExternalVisual)
	string_path tex_key;
	if (material_filter != -1)
		xr_sprintf(tex_key, "%s_m%d", short_name, material_filter);
	else
		xr_strcpy(tex_key, sizeof(tex_key), short_name);

	IReader* rd = FS.r_open(full_path);
	if (!rd) { Msg("! [gltf] skinned: can't open '%s'", full_path); return false; }
	const size_t blob_size = (size_t)rd->length();
	void* blob = xr_malloc(blob_size);
	CopyMemory(blob, rd->pointer(), blob_size);
	FS.r_close(rd);

	cgltf_options options = {};
	cgltf_data* gltf = NULL;
	if (cgltf_parse(&options, blob, blob_size, &gltf) != cgltf_result_success) { xr_free(blob); return false; }
	if (cgltf_load_buffers(&options, gltf, full_path) != cgltf_result_success)
	{
		cgltf_free(gltf);
		xr_free(blob);
		return false;
	}

	// Phase 1 front-door gate (see ext_gltf_unsupported): sparse / Draco / meshopt / unsupported-ext.
	if (ext_gltf_unsupported(gltf, full_path))
	{
		cgltf_free(gltf);
		xr_free(blob);
		return false;
	}

	const u16 nb = (u16)skin.bones.size();

	// glTF RH -> engine LH coordinate conversion (Z-flip). MUST match GetSkinData (which conjugates the
	// bones by the same C) so verts and bones share one space.
	Fmatrix C;
	C.identity();
	C._33 = -1.f;

	// --- accumulate skinned vertices (vertBoned4W: pos/normal/tangent/uv + bone ids + weights) -------
	xr_vector<vertBoned4W> verts;
	xr_vector<u32> indices;
	Fbox bb;
	bb.invalidate();
	const cgltf_image* base_img = NULL;
	bool base_color_captured = false; // capture baseColorFactor once (first material in the filtered set)

	xr_vector<const cgltf_node*> scene_nodes;
	ext_scene_nodes(gltf, scene_nodes);
	for (const cgltf_node* _np : scene_nodes)
	{
		const cgltf_node& node = *_np;
		if (!node.mesh)
			continue;
		// NOTE: skinned meshes are positioned by their JOINTS, not the mesh node transform (glTF spec),
		// so -- unlike the static path -- we do NOT bake cgltf_node_transform_world into the vertices.
		const cgltf_mesh& mesh = *node.mesh;
		for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
		{
			const cgltf_primitive& prim = mesh.primitives[pi];
			if (prim.type != cgltf_primitive_type_triangles)
				continue;

			const int prim_mat = prim.material ? (int)(prim.material - gltf->materials) : -1;
			if (material_filter == -2) { if (prim_mat != -1) continue; }
			else if (material_filter >= 0) { if (prim_mat != material_filter) continue; }

			const cgltf_accessor* a_pos = NULL; const cgltf_accessor* a_nrm = NULL;
			const cgltf_accessor* a_uv = NULL;  const cgltf_accessor* a_tan = NULL;
			const cgltf_accessor* a_joints = NULL; const cgltf_accessor* a_weights = NULL;
			for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
			{
				const cgltf_attribute& at = prim.attributes[ai];
				switch (at.type)
				{
				case cgltf_attribute_type_position: a_pos = at.data; break;
				case cgltf_attribute_type_normal:   a_nrm = at.data; break;
				case cgltf_attribute_type_texcoord: if (at.index == 0) a_uv = at.data; break;
				case cgltf_attribute_type_tangent:  a_tan = at.data; break;
				case cgltf_attribute_type_joints:   if (at.index == 0) a_joints = at.data; break;
				case cgltf_attribute_type_weights:  if (at.index == 0) a_weights = at.data; break;
				default: break;
				}
			}
			if (!a_pos)
				continue;

			if (!base_color_captured && prim.material && prim.material->has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
				if (!base_img && pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
					base_img = pbr.base_color_texture.texture->image;
				m_base_color.set(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
				m_base_alpha = pbr.base_color_factor[3];                                          // 4.3 skinned MASK
				m_alpha_cutoff = (prim.material->alpha_mode == cgltf_alpha_mode_mask) ? prim.material->alpha_cutoff : -1.f;
				base_color_captured = true;
			}

			const u32 v_base = (u32)verts.size();
			const cgltf_size v_count = a_pos->count;
			verts.reserve(verts.size() + v_count);
			bool read_warned = false;
			for (cgltf_size i = 0; i < v_count; ++i)
			{
				Fvector P, N, T; P.set(0, 0, 0); N.set(0, 1, 0); T.set(1, 0, 0);
				float uv[2] = {0, 0}; float tan4[4] = {1, 0, 0, 1};
				// 1.1 -- guard the residual corrupt-accessor case (sparse already gated). Log once, keep default.
				if (!cgltf_accessor_read_float(a_pos, i, &P.x, 3) && !read_warned)
				{
					Msg("! [gltf] skinned '%s' POSITION read failed (corrupt accessor)", full_path);
					read_warned = true;
				}
				if (a_nrm) cgltf_accessor_read_float(a_nrm, i, &N.x, 3);
				if (a_uv) cgltf_accessor_read_float(a_uv, i, uv, 2);
				if (a_tan) { cgltf_accessor_read_float(a_tan, i, tan4, 4); T.set(tan4[0], tan4[1], tan4[2]); }

				cgltf_uint ji[4] = {0, 0, 0, 0};
				float wt[4] = {1, 0, 0, 0};
				if (a_joints)  cgltf_accessor_read_uint(a_joints, i, ji, 4);
				if (a_weights) cgltf_accessor_read_float(a_weights, i, wt, 4);
				if (!a_joints || !a_weights) { ji[0] = ji[1] = ji[2] = ji[3] = 0; wt[0] = 1; wt[1] = wt[2] = wt[3] = 0; }

				// glTF RH -> engine LH coordinate conversion (bones conjugated by the same C in GetSkinData)
				{ Fvector t; C.transform_tiny(t, P); P = t; }
				{ Fvector t; C.transform_dir(t, N); N = t; }
				{ Fvector t; C.transform_dir(t, T); T = t; }
				Fvector B; B.crossproduct(N, T); B.mul(tan4[3]);

				// normalize the 4 weights to sum 1 (the skinning VS derives w3 = 1-w0-w1-w2)
				float wsum = wt[0] + wt[1] + wt[2] + wt[3];
				if (wsum > 1e-6f) { float inv = 1.f / wsum; wt[0] *= inv; wt[1] *= inv; wt[2] *= inv; wt[3] *= inv; }
				else { wt[0] = 1; wt[1] = wt[2] = wt[3] = 0; }

				vertBoned4W vx;
				vx.P = P; vx.N = N; vx.T = T; vx.B = B; vx.u = uv[0]; vx.v = uv[1];
				for (int k = 0; k < 4; ++k)
				{
					// glTF JOINTS_0 indexes skin.joints, which == our bone array order, so it's the bone id
					u16 bid = (u16)ji[k];
					if (bid >= nb) bid = 0; // defensive clamp (out-of-range joint -> root)
					vx.m[k] = bid;
				}
				vx.w[0] = wt[0]; vx.w[1] = wt[1]; vx.w[2] = wt[2];
				verts.push_back(vx);
				bb.modify(P);
			}

			const cgltf_size n = prim.indices ? prim.indices->count : v_count;
			indices.reserve(indices.size() + n);
			bool idx_warned = false;
			for (cgltf_size i = 0; i + 3 <= n; i += 3)
			{
				const cgltf_size ia = prim.indices ? cgltf_accessor_read_index(prim.indices, i + 0) : i + 0;
				const cgltf_size ib = prim.indices ? cgltf_accessor_read_index(prim.indices, i + 1) : i + 1;
				const cgltf_size ic = prim.indices ? cgltf_accessor_read_index(prim.indices, i + 2) : i + 2;
				// 1.4 index sanity (see static path): skip out-of-range triangles, log once per primitive.
				if (ia >= v_count || ib >= v_count || ic >= v_count)
				{
					if (!idx_warned)
					{
						Msg("! [gltf] skinned '%s' index >= vertex count (%u) -- skipping triangle(s)", full_path, (u32)v_count);
						idx_warned = true;
					}
					continue;
				}
				const u32 a = (u32)(v_base + ia);
				const u32 b = (u32)(v_base + ib);
				const u32 c = (u32)(v_base + ic);
#if EXTERNAL_FLIP_WINDING
				indices.push_back(a); indices.push_back(c); indices.push_back(b);
#else
				indices.push_back(a); indices.push_back(b); indices.push_back(c);
#endif
			}
		}
	}

	// decode the base-color image BEFORE cgltf_free (its bytes live in gltf-owned buffer memory)
	ID3DBaseTexture* decoded_tex = base_img ? ext_decode_gltf_image(base_img, full_path) : NULL;

	cgltf_free(gltf);
	xr_free(blob);

	if (verts.empty() || indices.empty())
	{
		_RELEASE(decoded_tex);
		Msg("! [gltf] skinned '%s' (filter %d) selected no triangle geometry", full_path, material_filter);
		return false;
	}
	if (verts.size() > 65535)
	{
		// _Render draws via RCache (16-bit IB); the 32-bit rebind trick the static path uses isn't wired
		// into the inherited skinned render. Skinned meshes this large are rare -> Phase A limitation.
		_RELEASE(decoded_tex);
		Msg("! [gltf] skinned '%s' has %u verts (>65535) -- not supported yet", full_path, (u32)verts.size());
		return false;
	}

	// --- index buffer (16-bit; the engine builds the skinned VB from our vertBoned4W via _Load_hw) ----
	vBase = 0; vCount = (u32)verts.size(); iBase = 0; iCount = (u32)indices.size(); dwPrimitives = iCount / 3;
	{
		xr_vector<u16> i16; i16.resize(iCount);
		for (u32 k = 0; k < iCount; ++k) i16[k] = (u16)indices[k];
		VERIFY(NULL == p_rm_Indices);
#if defined(USE_DX10) || defined(USE_DX11)
		R_CHK(dx10BufferUtils::CreateIndexBuffer(&p_rm_Indices, i16.data(), iCount * 2));
		HW.stats_manager.increment_stats_ib(p_rm_Indices);
		// CPU index replica (DX10/11 can't read the GPU IB) -- mirrors CSkeletonX::_DuplicateIndices.
		// REQUIRED by _CollectBoneFaces (bone-face build) and _PickBone (IK foot collider raycast); without
		// it the pick path dereferences a null index buffer and crashes.
		m_Indices.create(crc32(i16.data(), iCount * sizeof(u16)), iCount, i16.data());
#else // DX9
		const BOOL bSoft = HW.Caps.geometry.bSoftware;
		const u32 dwUsage = (bSoft ? D3DUSAGE_SOFTWAREPROCESSING : 0);
		BYTE* bytes = 0;
		R_CHK(HW.pDevice->CreateIndexBuffer(iCount * 2, dwUsage, D3DFMT_INDEX16, D3DPOOL_MANAGED, &p_rm_Indices, 0));
		HW.stats_manager.increment_stats_ib(p_rm_Indices);
		R_CHK(p_rm_Indices->Lock(0, 0, (void**)&bytes, 0));
		CopyMemory(bytes, i16.data(), iCount * 2);
		p_rm_Indices->Unlock();
#endif
	}

	// --- skinning state (read by the inherited CSkeletonX::_Render) ------------------------------------
	RenderMode = RM_SKINNING_4B;
	RMS_bonecount = nb; // upload every bone (incl synth root) into sbones_array; nb<=65 fits the 78-bone array
	{
		// BonesUsed drives has_visible_bones (culling). Collect the unique referenced bone ids.
		xr_vector<u16> used;
		for (u32 vi = 0; vi < verts.size(); ++vi)
			for (int k = 0; k < 4; ++k)
			{
				const u16 bid = verts[vi].m[k];
				if (used.end() == std::find(used.begin(), used.end(), bid)) used.push_back(bid);
			}
		const u32 crc = crc32(used.data(), used.size() * sizeof(u16));
		BonesUsed.create(crc, used.size(), used.data());
	}

	// --- albedo + shader (compile as SKIN_4) ----------------------------------------------------------
	// Resolve the base-color texture (decoded $user$ or the fallback), then create the shader with skinning
	// mode 4 ACTIVE so external_static.s's stock model VS (and our shadow VS) compile as the SKIN_4 variant.
	// Skinning is VS-only, so the same .s + deferred PS work unchanged. Reset the mode after (like OGF _Load).
	string_path tex_name;
	ref_texture user_tex; // must outlive SetShaderTexture
	bool resolved = false;
	if (decoded_tex)
	{
		ext_make_user_name(tex_name, tex_key);
		user_tex.create(tex_name);
		if (user_tex._get()) { user_tex->surface_set(decoded_tex); resolved = true; }
		_RELEASE(decoded_tex);
	}
	if (!resolved)
	{
		// factor-only material (no base_img) -> WHITE so albedo == baseColorFactor (BrainStem's flat
		// per-part colors); a real-but-failed texture -> the placeholder.
		const char* w = (!base_img) ? ext_white_texture_name() : NULL;
		xr_strcpy(tex_name, sizeof(tex_name), w ? w : EXTERNAL_FALLBACK_TEXTURE);
	}

	// SetShaderTexture internally does SetSkinningMode(this->skinning) (== 4, set above), so the dedicated
	// external_skinned.s compiles its model VS as SKIN_4. (The separate .s name also avoids reusing the
	// static path's cached SKIN_NONE compile of external_static.)
	SetShaderTexture(EXTERNAL_SKINNED_SHADER, tex_name);

	// --- HW vertex buffer: the engine packs vertBoned4W -> vertHW_4W (normal quantization + bone*3 index)
	// and creates rm_geom with dwDecl_4W. Needs RenderMode + vCount + p_rm_Indices already set (above).
	_Load_hw(*this, verts.data());

	// --- bounds ---------------------------------------------------------------------------------------
	vis.box.set(bb.min, bb.max);
	Fvector c, half;
	c.add(bb.min, bb.max).mul(0.5f);
	half.sub(bb.max, bb.min).mul(0.5f);
	vis.sphere.set(c, half.magnitude());

	Type = MT_EXTERNAL_SKINNED; // own type so Instance_Duplicate clones this as FExternalSkinned
	Msg("* [gltf] skinned child '%s' filter=%d: %u verts / %u tris, bones<=%u", short_name, material_filter,
		vCount, dwPrimitives, nb);
	return true;
}

//////////////////////////////////////////////////////////////////////
// Render
//////////////////////////////////////////////////////////////////////

void FExternalVisual::Render(float)
{
	PROF_EVENT("FExternalVisual::Render");
	// emissive overlay: push glTF emissiveFactor*strength so the emissive PS scales the map. The
	// shader is already bound (set_Element ran before Render), so this binds into the active table.
	// Push only the constant(s) this child's shader declares (set_c on a missing constant is a no-op, but
	// this keeps it clear). Child kinds are mutually exclusive: emissive overlay / blend surface / metal
	// overlay / lit deferred batch.
	if (m_emissive)
	{
		RCache.set_c("ext_emissive_scale", m_emissive_scale.x, m_emissive_scale.y, m_emissive_scale.z, 1.f);
	}
	else
	{
		// shared material params -- every lit/metal/blend child samples albedo/MR/normal through these
		RCache.set_c("ext_uv_transform", m_uv_xform.x, m_uv_xform.y, m_uv_xform.z, m_uv_xform.w); // KHR_texture_transform
		RCache.set_c("ext_uv_rot", m_uv_rot.x, m_uv_rot.y, 0.f, 0.f);                             // KHR_texture_transform rotation (cos,sin)
		RCache.set_c("ext_uv_set", m_uv_set.x, m_uv_set.y, m_uv_set.z, m_uv_set.w);               // 3.5 per-map texCoord (base/normal/mr/ao)
		RCache.set_c("ext_base_color", m_base_color.x, m_base_color.y, m_base_color.z, m_base_alpha); // baseColorFactor (a = MASK alpha)
		RCache.set_c("ext_mr_factor", m_mr_factor.x, m_mr_factor.y, m_mr_factor.z, 1.f);          // metallic/roughness/normalScale
		if (m_blend)
			RCache.set_c("ext_blend_alpha", m_blend_alpha, 0.f, 0.f, 0.f); // glTF baseColorFactor.a (opacity)
		else if (!m_metal) // lit deferred batch
		{
			RCache.set_c("ext_alpha_cutoff", m_alpha_cutoff, 0.f, 0.f, 0.f); // MASK cutoff (-1 = no clip)
			RCache.set_c("ext_ao_strength", m_ao_strength, 0.f, 0.f, 0.f);   // glTF occlusion (ORM in MR.r)
		}
		// metal overlay: only the shared params above
	}
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
	PCOPY(m_emissive);
	PCOPY(m_emissive_scale);
	PCOPY(m_metal);
	PCOPY(m_alpha_cutoff);
	PCOPY(m_ao_strength);
	PCOPY(m_blend);
	PCOPY(m_blend_alpha);
	PCOPY(m_base_color);
	PCOPY(m_mr_factor);
	PCOPY(m_uv_xform);
	PCOPY(m_uv_rot);
	PCOPY(m_base_alpha);
	PCOPY(m_uv_set);
}
