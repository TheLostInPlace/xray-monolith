#include "stdafx.h"
#pragma hdrstop
#include "TextureDescrManager.h"
#include "ETextureParams.h"
#include "profiler.h"
#include "../../xrCore/xr_ini.h"

// eye-params
float r__dtex_range = 50;

class cl_dt_scaler : public R_constant_setup
{
public:
	float scale;

	cl_dt_scaler(float s) : scale(s)
	{
	};

	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scale, scale, scale, 1 / r__dtex_range);
	}
};

void fix_texture_thm_name(LPSTR fn)
{
	LPSTR _ext = strext(fn);
	if (_ext &&
		(0 == stricmp(_ext, ".tga") ||
			0 == stricmp(_ext, ".thm") ||
			0 == stricmp(_ext, ".ltx") ||
			0 == stricmp(_ext, ".dds") ||
			0 == stricmp(_ext, ".bmp") ||
			0 == stricmp(_ext, ".ogm") ||
            0 == stricmp(_ext, ".gif")))
		*_ext = 0;
}

void CTextureDescrMngr::LoadTHM(LPCSTR initial, map_TD& s_texture_details, map_CS& s_detail_scalers)
{
	PROF_EVENT();

	FS_FileSet flist;
	FS.file_list(flist, initial, FS_ListFiles, "*.thm");

	STextureParams tp;
	string_path fn;

	for (const FS_File& fs_iter : flist)
	{
		FS.update_path(fn, initial, fs_iter.name.c_str());
		IReader* F = FS.r_open(fn);
		xr_strcpy(fn, fs_iter.name.c_str());
		fix_texture_thm_name(fn);

		R_ASSERT(F->find_chunk(THM_CHUNK_TYPE));
		F->r_u32();
		tp.Clear();
		tp.Load(*F);
		FS.r_close(F);
		if (STextureParams::ttImage == tp.type || STextureParams::ttTerrain == tp.type || STextureParams::ttNormalMap ==
			tp.type)
		{
			texture_desc& desc = s_texture_details[fn];
			cl_dt_scaler*& dts = s_detail_scalers[fn];

			if (tp.detail_name.size() && tp.flags.is_any(STextureParams::flDiffuseDetail | STextureParams::flBumpDetail)
			)
			{
				if (desc.m_assoc)
					xr_delete(desc.m_assoc);

				desc.m_assoc = xr_new<texture_assoc>();
				desc.m_assoc->detail_name = tp.detail_name;
				if (dts)
					dts->scale = tp.detail_scale;
				else
					/*desc.m_assoc->cs*/dts = xr_new<cl_dt_scaler>(tp.detail_scale);

				desc.m_assoc->usage = 0;

				if (tp.flags.is(STextureParams::flDiffuseDetail))
					desc.m_assoc->usage |= (1 << 0);

				if (tp.flags.is(STextureParams::flBumpDetail))
					desc.m_assoc->usage |= (1 << 1);
			}
			if (desc.m_spec)
				xr_delete(desc.m_spec);

			desc.m_spec = xr_new<texture_spec>();
			desc.m_spec->m_material = tp.material + (tp.material < 4 ? tp.material_weight : 0);
			desc.m_spec->m_use_steep_parallax = false;

			if (tp.bump_mode == STextureParams::tbmUse)
			{
				desc.m_spec->m_bump_name = tp.bump_name;
			}
			else if (tp.bump_mode == STextureParams::tbmUseParallax)
			{
				desc.m_spec->m_bump_name = tp.bump_name;
				desc.m_spec->m_use_steep_parallax = true;
			}
		}
	}
}

// ltx material words, the thm materials and the pbr slot
static const struct
{
	LPCSTR name;
	float value;
} ltx_materials[] = {
	{"orennayar_blin", float(STextureParams::tmOrenNayar_Blin)},
	{"blin_phong", float(STextureParams::tmBlin_Phong)},
	{"phong_metal", float(STextureParams::tmPhong_Metal)},
	{"metal_orennayar", float(STextureParams::tmMetal_OrenNayar)},
	{"pbr", 4.5f},
};

static bool parse_ltx_number(LPCSTR value, float& number)
{
	char* end;
	const float v = strtof(value, &end);
	if (end == value || *end || !_valid(v))
		return false;
	number = v;
	return true;
}

static bool parse_ltx_material(LPCSTR value, float& material)
{
	for (const auto& m : ltx_materials)
	{
		if (!stricmp(value, m.name))
		{
			material = m.value;
			return true;
		}
	}
	return parse_ltx_number(value, material);
}

// an absent key keeps the default, an empty or bad value fails
static bool read_ltx_number(const CInifile& ini, LPCSTR key, float& number)
{
	if (!ini.line_exist("texture", key))
		return true;
	LPCSTR value = ini.r_string("texture", key);
	return value && parse_ltx_number(value, number);
}

static bool read_ltx_flag(const CInifile& ini, LPCSTR key, bool& flag)
{
	if (!ini.line_exist("texture", key))
		return true;
	if (!ini.r_string("texture", key))
		return false;
	flag = !!ini.r_bool("texture", key);
	return true;
}

void CTextureDescrMngr::LoadLTX(LPCSTR initial, map_TD& s_texture_details, map_CS& s_detail_scalers)
{
	PROF_EVENT();

	FS_FileSet flist;
	FS.file_list(flist, initial, FS_ListFiles, "*.ltx");
	for (const FS_File& file : flist)
	{
		string_path name;
		xr_strcpy(name, file.name.c_str());
		fix_texture_thm_name(name);

		string_path path;
		FS.update_path(path, initial, file.name.c_str());
		CInifile ini(path, TRUE, TRUE, FALSE);
		if (!ini.section_exist("texture"))
			continue;

		LPCSTR mode = ini.line_exist("texture", "bump_mode")
			? ini.r_string("texture", "bump_mode") : "none";
		if (!mode || (stricmp(mode, "none") && stricmp(mode, "use") && stricmp(mode, "parallax")))
		{
			Msg("! Invalid texture bump_mode in '%s': expected none, use or parallax", path);
			continue;
		}

		const bool use_bump = stricmp(mode, "none") != 0;
		string_path bump_name = {};
		if (use_bump)
		{
			LPCSTR configured_name = ini.line_exist("texture", "bump_name")
				? ini.r_string("texture", "bump_name") : nullptr;
			if (configured_name && configured_name[0])
			{
				xr_strcpy(bump_name, configured_name);
				fix_texture_thm_name(bump_name);
			}
			else
				strconcat(sizeof(bump_name), bump_name, name, "_bump");
		}

		float material = 1.0f;
		if (ini.line_exist("texture", "material"))
		{
			LPCSTR value = ini.r_string("texture", "material");
			if (!value || !parse_ltx_material(value, material))
			{
				Msg("! Invalid texture material in '%s': expected a number, pbr or a thm material", path);
				continue;
			}
		}
		float material_weight = 0.0f, detail_scale = 1.0f;
		bool diffuse_detail = false, bump_detail = false;
		if (!read_ltx_number(ini, "material_weight", material_weight) || !read_ltx_number(ini, "detail_scale", detail_scale) ||
			!read_ltx_flag(ini, "diffuse_detail", diffuse_detail) || !read_ltx_flag(ini, "bump_detail", bump_detail))
		{
			Msg("! Invalid texture value in '%s': material_weight and detail_scale take numbers, diffuse_detail and bump_detail take true or false", path);
			continue;
		}

		LPCSTR detail_name = ini.line_exist("texture", "detail_name")
			? ini.r_string("texture", "detail_name") : nullptr;
		u8 detail_usage = 0;
		if (diffuse_detail)
			detail_usage |= (1 << 0);
		if (bump_detail)
			detail_usage |= (1 << 1);

		// replaces the whole thm entry
		texture_desc& desc = s_texture_details[name];
		if (desc.m_assoc)
			xr_delete(desc.m_assoc);
		if (detail_name && detail_name[0] && detail_usage)
		{
			desc.m_assoc = xr_new<texture_assoc>();
			desc.m_assoc->detail_name = detail_name;
			desc.m_assoc->usage = detail_usage;

			cl_dt_scaler*& dts = s_detail_scalers[name];
			if (dts)
				dts->scale = detail_scale;
			else
				dts = xr_new<cl_dt_scaler>(detail_scale);
		}

		if (desc.m_spec)
			xr_delete(desc.m_spec);
		desc.m_spec = xr_new<texture_spec>();
		desc.m_spec->m_material = material + (material < 4 ? material_weight : 0);
		desc.m_spec->m_bump_name = bump_name;
		desc.m_spec->m_use_steep_parallax = stricmp(mode, "parallax") == 0;
	}
}

void CTextureDescrMngr::Load()
{
	LoadTHM("$game_textures$", m_texture_details, m_detail_scalers);
	LoadTHM("$level$", m_texture_details, m_detail_scalers);
	LoadLTX("$game_textures$", m_texture_details, m_detail_scalers);
	LoadLTX("$level$", m_texture_details, m_detail_scalers);
}

void CTextureDescrMngr::UnLoad()
{
	for (auto& it : m_texture_details)
	{
		xr_delete(it.second.m_assoc);
		xr_delete(it.second.m_spec);
	}
	m_texture_details.clear();
}

CTextureDescrMngr::~CTextureDescrMngr()
{
	map_CS::iterator I = m_detail_scalers.begin();
	map_CS::iterator E = m_detail_scalers.end();

	for (; I != E; ++I)
		xr_delete(I->second);

	m_detail_scalers.clear();
}

shared_str CTextureDescrMngr::GetBumpName(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_bump_name;
		}
	}
	return "";
}

BOOL CTextureDescrMngr::UseSteepParallax(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_use_steep_parallax;
		}
	}
	return FALSE;
}

float CTextureDescrMngr::GetMaterial(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_material;
		}
	}
	return 1.0f;
}

void CTextureDescrMngr::GetTextureUsage(const shared_str& tex_name, BOOL& bDiffuse, BOOL& bBump) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_assoc)
		{
			u8 usage = I->second.m_assoc->usage;
			bDiffuse = !!(usage & (1 << 0));
			bBump = !!(usage & (1 << 1));
		}
	}
}

BOOL CTextureDescrMngr::GetDetailTexture(const shared_str& tex_name, LPCSTR& res, R_constant_setup* & CS) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_assoc)
		{
			texture_assoc* TA = I->second.m_assoc;
			res = TA->detail_name.c_str();
			map_CS::const_iterator It2 = m_detail_scalers.find(tex_name);
			CS = It2 == m_detail_scalers.end() ? 0 : It2->second; //TA->cs;
			return TRUE;
		}
	}
	return FALSE;
}
