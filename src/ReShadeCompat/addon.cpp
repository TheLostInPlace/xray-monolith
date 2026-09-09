#include "stdafx.h"
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#pragma warning(disable: 4047)
HINSTANCE hInstance = (HINSTANCE)&__ImageBase;
#pragma warning(default: 4047)

//Credits for idea SSE ReShade Helper https://www.nexusmods.com/skyrimspecialedition/mods/78961

static reshade::api::effect_runtime* m_runtime = nullptr;
static reshade::api::command_list* m_cmdlist = nullptr;
static reshade::api::resource_view m_rtv;
static reshade::api::resource_view m_rtv_srgb;

// changes whenever a different runtime object arrives so the renderer can re declare its colour space
static unsigned m_runtime_gen = 0;

static void on_reshade_begin_effects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb)
{
	if (m_runtime != runtime)
		++m_runtime_gen;

	m_runtime = runtime;
	m_cmdlist = cmd_list;
	m_rtv = rtv;
	m_rtv_srgb = rtv_srgb;
}

void render_reshade_effects()
{
	if (m_runtime)
		m_runtime->render_effects(m_cmdlist, m_rtv, m_rtv_srgb);
}

// declares the presented colour space to the runtime, false when it is not up yet
bool reshade_publish_color_space(bool hdr10)
{
	if (!m_runtime)
		return false;

	m_runtime->set_color_space(hdr10 ? reshade::api::color_space::hdr10_st2084
	                                 : reshade::api::color_space::unknown);
	return true;
}

// non zero while a runtime is up, a different value after every runtime replacement
unsigned reshade_runtime_generation()
{
	return m_runtime ? m_runtime_gen : 0;
}

bool init_reshade()
{
	if (!reshade::register_addon(hInstance))
		return false;

	reshade::register_event<reshade::addon_event::reshade_begin_effects>(on_reshade_begin_effects);

	return true;
}

void unregister_reshade()
{
	reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(on_reshade_begin_effects);
	reshade::unregister_addon(hInstance);
	m_runtime = nullptr;
	m_cmdlist = nullptr;
	m_rtv.handle = 0;
	m_rtv_srgb.handle = 0;
}