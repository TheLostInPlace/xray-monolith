#include "stdafx.h"
#pragma hdrstop

#include "renderdoc_integration.h"
#include "renderdoc_app.h"
#include "IGame_Level.h"

namespace
{
	const char* const rdc_capture_folder = "renderdoc\\captures\\";

	RENDERDOC_API_1_7_0* s_rdc_api = nullptr;
	RENDERDOC_Version s_rdc_version = eRENDERDOC_API_Version_1_0_0;
	bool s_rdc_annotations = false;
	bool s_rdc_initialized = false;
	u32 s_rdc_seen_captures = 0;
	string_path s_rdc_capture_template = {};
	xr_string s_rdc_latest_capture;

	RENDERDOC_DevicePointer s_rdc_device = nullptr;
	RENDERDOC_WindowHandle s_rdc_window = nullptr;

	wchar_t s_rdc_region_name[64] = {};
	string64 s_rdc_region_label = {};
	u32 s_rdc_region_depth = 0;
	bool s_rdc_region_capturing = false;

	const char* const rdc_marker_names[] = {
		"CRender_Render", "render_menu", "DEFER_PART0_SPLIT", "DEFER_TEST_LIGHT_VIS", "DEFER_PART1_SPLIT",
		"DEFER_WALLMARKS", "MARK_MSAA_EDGES", "DEFER_RAIN", "DEFER_SUN", "DEFER_SELF_ILLUM",
		"DEFER_LIGHT_NO_OCCQ", "DEFER_LIGHT_OCCQ", "DEFER_LIGHT_COMBINE", "phase_scene_prepare",
		"SHADOWED_LIGHTS", "PHASE_VIS_UPDATE", "PHASE_CALC_POOLS", "GENERATE_SHMAPS", "RENDER_SHADOWS",
		"UNSHADOWED_LIGHTS", "POINT_LIGHTS_ACCUM_UNSH", "SPOT_LIGHTS_ACCUM_UNSH", "SE_SUN_NEAR",
		"SE_SUN_NEAR_MINMAX_GENERATE", "SE_SUN_NEAR_sub_phase", "Perform_lighting", "accum_direct_blend",
		"accum_direct_f", "accum_direct_lum", "accum_direct_volumetric", "render_rain", "phase_ssfx_ao",
		"phase_bloom", "phase_ssfx_bloom", "phase_combine", "combine_1", "combine_2", "phase_pp",
		"phase_ssfx_ssr", "Forward_rendering", "render_distort_objects", "phase_sunshafts",
		"phase_ssfx_fog_scattering", "phase_ssfx_motion_blur", "phase_blur", "phase_dof", "phase_lut",
		"SMAA", "phase_combine_volumetric", "simulate_fluid", "render_fluid", "AttachFluidData",
		"DetachAndSwapFluidData", "AdvectColorBFECC", "AdvectColor", "AdvectVelocity",
		"ApplyVorticityConfinement", "ApplyExternalForces", "ComputeVelocityDivergence", "ComputePressure",
		"ProjectVelocity", "Fluid_update_obstacles", "ProcessObstacles", "RenderObstacle",
		"RenderDynamicObstacle"};

	HMODULE rdc_acquire_module()
	{
		HMODULE module = GetModuleHandleW(L"renderdoc.dll");
		if (module || !Core.ParamsData.test(ECoreParams::renderdoc))
			return module;

		wchar_t path[MAX_PATH * 4] = {};
		const DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
		wchar_t* const leaf = (length && length < std::size(path)) ? wcsrchr(path, L'\\') : nullptr;
		if (!leaf || wcscpy_s(leaf + 1, std::size(path) - size_t(leaf + 1 - path), L"renderdoc.dll"))
		{
			Msg("! [RDC] executable path unavailable, capture disabled");
			return nullptr;
		}

		module = LoadLibraryExW(path, nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!module)
			Msg("~ [RDC] renderdoc.dll not loaded from the game bin folder (%lu), capture disabled",
				GetLastError());
		return module;
	}

	// GetAPI answers 1 only for a version it implements, so walk down until one is accepted
	bool rdc_negotiate_api(pRENDERDOC_GetAPI get_api)
	{
		static const RENDERDOC_Version versions[] = {
			eRENDERDOC_API_Version_1_7_0,
			eRENDERDOC_API_Version_1_6_0,
			eRENDERDOC_API_Version_1_5_0,
			eRENDERDOC_API_Version_1_4_0,
			eRENDERDOC_API_Version_1_1_0};

		for (const RENDERDOC_Version version : versions)
		{
			void* api = nullptr;
			if (get_api(version, &api) == 1 && api)
			{
				s_rdc_api = static_cast<RENDERDOC_API_1_7_0*>(api);
				s_rdc_version = version;
				s_rdc_annotations = version >= eRENDERDOC_API_Version_1_7_0;
				return true;
			}
		}
		return false;
	}

	bool rdc_available()
	{
		if (s_rdc_api)
			return true;

		Msg("~ [RDC] renderdoc.dll is not loaded, inject with RenderDoc or launch with -renderdoc");
		return false;
	}

	bool rdc_ui_connected()
	{
		return s_rdc_api && s_rdc_api->IsTargetControlConnected() == 1;
	}

	void rdc_apply_capture_options()
	{
		struct option_key
		{
			ECoreParams param;
			RENDERDOC_CaptureOption option;
			const char* name;
		};
		static const option_key keys[] = {
			{ECoreParams::rdoc_refall, eRENDERDOC_Option_RefAllResources, "reference all resources"},
			{ECoreParams::rdoc_cmdlists, eRENDERDOC_Option_CaptureAllCmdLists, "capture all command lists"}};

		for (const option_key& key : keys)
		{
			if (!Core.ParamsData.test(key.param))
				continue;

			s_rdc_api->SetCaptureOptionU32(key.option, 1);
			Msg("* [RDC] option %s now %u", key.name, s_rdc_api->GetCaptureOptionU32(key.option));
		}
	}

	bool rdc_ensure_directory(const char* path)
	{
		VerifyPath(path);

		const DWORD attributes = GetFileAttributesA(path);
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	// RenderDoc appends _frameN.rdc to the template and takes a re-apply at any time
	void rdc_prepare_capture_path()
	{
		string_path executable = {};
		if (!GetModuleFileNameA(nullptr, executable, sizeof(executable)))
			return;

		char* const leaf = strrchr(executable, '\\');
		if (!leaf)
			return;
		*leaf = 0;

		char* const extension = strrchr(leaf + 1, '.');
		if (extension)
			*extension = 0;

		string_path directory = {};
		const bool data_root = FS.path_exist("$app_data_root$");
		if (data_root)
			FS.update_path(directory, "$app_data_root$", rdc_capture_folder);

		if (!data_root || !rdc_ensure_directory(directory))
		{
			xr_sprintf(directory, "%s\\%s", executable, rdc_capture_folder);
			rdc_ensure_directory(directory);
			Msg("~ [RDC] engine data root unusable, captures land in %s", directory);
		}

		xr_sprintf(s_rdc_capture_template, "%s%s", directory, leaf + 1);
		s_rdc_api->SetCaptureFilePathTemplate(s_rdc_capture_template);
	}

	// D3D11 takes a null device here, the annotation rides the immediate command stream
	void rdc_annotate(const char* key, RENDERDOC_AnnotationType type, u32 width,
		const RENDERDOC_AnnotationValue* value)
	{
		const u32 result = s_rdc_api->SetCommandAnnotation(nullptr, nullptr, key, type, width, value);

		static bool reported = false;
		if (result && !reported)
		{
			reported = true;
			Msg("! [RDC] SetCommandAnnotation failed %u on %s", result, key);
		}
	}

	void rdc_annotate(const char* key, u32 number)
	{
		rdc_annotate(key, eRENDERDOC_UInt32, 0, RDAnnotationHelper(number));
	}

	void rdc_annotate(const char* key, float number)
	{
		rdc_annotate(key, eRENDERDOC_Float, 0, RDAnnotationHelper(number));
	}

	void rdc_annotate(const char* key, const char* text)
	{
		rdc_annotate(key, eRENDERDOC_String, 0, RDAnnotationHelper(text));
	}

	void rdc_annotate(const char* key, const float* floats, u32 width)
	{
		RENDERDOC_AnnotationValue value = {};
		for (u32 index = 0; index < width; ++index)
			value.vector.float32[index] = floats[index];

		rdc_annotate(key, eRENDERDOC_Float, width, &value);
	}

	// Marker names are ascii identifiers so a widening loop covers every one of them
	bool rdc_widen(const char* text, wchar_t* out, size_t count)
	{
		size_t index = 0;
		for (; text[index] && index + 1 < count; ++index)
			out[index] = wchar_t(u8(text[index]));

		out[index] = 0;
		return index != 0 && !text[index];
	}

	void rdc_print_marker_names()
	{
		Msg("* [RDC] marker regions available to rdoc_capture_region");

		string512 line = {};
		for (const char* const name : rdc_marker_names)
		{
			if (xr_strlen(line) + xr_strlen(name) + 2 >= 96)
			{
				Msg("  %s", line);
				line[0] = 0;
			}

			if (line[0])
				xr_strcat(line, "  ");
			xr_strcat(line, name);
		}

		if (line[0])
			Msg("  %s", line);
	}

	void rdc_log_capture(u32 index)
	{
		u32 length = 0;
		u64 timestamp = 0;
		if (!s_rdc_api->GetCapture(index, nullptr, &length, &timestamp) || !length)
			return;

		xr_vector<char> path(size_t(length) + 1, 0);
		if (!s_rdc_api->GetCapture(index, path.data(), &length, &timestamp))
			return;

		// GetCapture hands back the absolute path already
		s_rdc_latest_capture = path.data();
		Msg("* [RDC] capture written %s  timestamp %llu", path.data(), timestamp);
	}
}

void renderdoc_initialize()
{
	if (s_rdc_initialized)
		return;
	s_rdc_initialized = true;

	const HMODULE module = rdc_acquire_module();
	if (!module)
		return;

	const pRENDERDOC_GetAPI get_api =
		reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
	if (!get_api || !rdc_negotiate_api(get_api))
	{
		Msg("! [RDC] API negotiation failed, capture disabled");
		return;
	}

	// The engine keeps its own crash handler and minidump pipeline
	s_rdc_api->UnloadCrashHandler();

	rdc_apply_capture_options();
	rdc_prepare_capture_path();
	s_rdc_seen_captures = s_rdc_api->GetNumCaptures();

	int major = 0;
	int minor = 0;
	int patch = 0;
	s_rdc_api->GetAPIVersion(&major, &minor, &patch);
	Msg("* [RDC] capture API %d.%d.%d ready, ui %s, annotations %s", major, minor, patch,
		rdc_ui_connected() ? "connected" : "not connected",
		s_rdc_annotations ? "available" : "unavailable");
	Msg("* [RDC] captures land in %s_frameN.rdc", s_rdc_capture_template);
}

bool renderdoc_api_live()
{
	return s_rdc_api != nullptr;
}

void renderdoc_trigger_capture(u32 frames)
{
	if (!rdc_available())
		return;

	if (frames > 1)
		s_rdc_api->TriggerMultiFrameCapture(frames);
	else
		s_rdc_api->TriggerCapture();

	Msg("* [RDC] queued %u frame(s), each one writes its own rdc file, ui %s", frames,
		rdc_ui_connected() ? "connected" : "not connected");
}

void renderdoc_poll_captures()
{
	if (!s_rdc_api)
		return;

	const u32 count = s_rdc_api->GetNumCaptures();
	for (; s_rdc_seen_captures < count; ++s_rdc_seen_captures)
		rdc_log_capture(s_rdc_seen_captures);
}

void renderdoc_open_replay_ui()
{
	if (!rdc_available())
		return;

	if (rdc_ui_connected() && s_rdc_version >= eRENDERDOC_API_Version_1_5_0)
	{
		const u32 shown = s_rdc_api->ShowReplayUI();
		Msg("%s [RDC] connected ui raise %s", shown ? "*" : "~", shown ? "accepted" : "refused");
		return;
	}

	// The path rides a command line so quotes keep a spaced folder in one argument
	const xr_string quoted = s_rdc_latest_capture.empty()
		? xr_string()
		: xr_string("\"" + s_rdc_latest_capture + "\"");
	const char* const capture = quoted.empty() ? nullptr : quoted.c_str();
	const u32 pid = s_rdc_api->LaunchReplayUI(1, capture);
	if (pid)
		Msg("* [RDC] replay ui launched pid %u %s", pid, capture ? capture : "with no capture");
	else
		Msg("! [RDC] replay ui launch failed");
}

void renderdoc_set_overlay(bool visible)
{
	if (!rdc_available())
		return;

	s_rdc_api->MaskOverlayBits(0, visible ? (eRENDERDOC_Overlay_Default | eRENDERDOC_Overlay_Enabled) : 0);
	Msg("* [RDC] overlay bits 0x%08x", s_rdc_api->GetOverlayBits());
}

bool renderdoc_overlay_enabled()
{
	if (!rdc_available())
		return false;

	return (s_rdc_api->GetOverlayBits() & eRENDERDOC_Overlay_Enabled) != 0;
}

void renderdoc_annotate_frame(const Fvector4* shader_params, u32 count)
{
	if (!s_rdc_annotations || s_rdc_api->IsFrameCapturing() != 1)
		return;

	rdc_annotate("xray.frame.number", Device.dwFrame);
	rdc_annotate("xray.frame.time", Device.fTimeGlobal);

	rdc_annotate("xray.camera.position", &Device.vCameraPosition.x, 3);
	rdc_annotate("xray.camera.direction", &Device.vCameraDirection.x, 3);
	rdc_annotate("xray.camera.fov", Device.fFOV);
	rdc_annotate("xray.camera.aspect", Device.fASPECT);

	rdc_annotate("xray.render.width", Device.dwWidth);
	rdc_annotate("xray.render.height", Device.dwHeight);
	rdc_annotate("xray.svp", Device.m_SecondViewport.IsSVPFrame() ? 1u : 0u);

	rdc_annotate("xray.level.name", g_pGameLevel ? g_pGameLevel->name().c_str() : "");

	for (u32 index = 0; index < count && shader_params; ++index)
	{
		string64 key = {};
		xr_sprintf(key, "xray.shader_param.%u", index + 1);
		rdc_annotate(key, &shader_params[index].x, 4);
	}
}

void renderdoc_set_active_window(void* device, void* window)
{
	// SetActiveWindow rejects a null handle, unlike the rest of the API
	if (!s_rdc_api || !device || !window)
		return;

	s_rdc_api->SetActiveWindow(device, window);

	s_rdc_device = device;
	s_rdc_window = window;
}

bool g_rdoc_marker_watch = false;

void renderdoc_capture_region(const char* marker)
{
	if (!marker || !marker[0])
	{
		rdc_print_marker_names();
		return;
	}

	if (!rdc_available())
		return;

	if (!s_rdc_device || !s_rdc_window)
	{
		Msg("! [RDC] the renderer has no device yet, region capture needs a running frame");
		return;
	}

	// A second request mid capture would reset the depth and leave the open one unmatched
	if (s_rdc_region_capturing)
	{
		Msg("! [RDC] region %s is capturing already, wait for it to finish", s_rdc_region_label);
		return;
	}

	if (!rdc_widen(marker, s_rdc_region_name, std::size(s_rdc_region_name)))
	{
		s_rdc_region_name[0] = 0;
		Msg("! [RDC] marker name %s does not fit", marker);
		return;
	}

	xr_strcpy(s_rdc_region_label, marker);
	s_rdc_region_depth = 0;
	s_rdc_region_capturing = false;
	g_rdoc_marker_watch = true;
	Msg("* [RDC] armed on marker %s, the next outermost region writes one capture", marker);
}

bool renderdoc_marker_begin(const wchar_t* name)
{
	if (!s_rdc_region_name[0] || wcscmp(name, s_rdc_region_name))
		return false;

	// Only the outermost instance opens, the inner ones just carry the depth
	if (s_rdc_region_depth++ || s_rdc_api->IsFrameCapturing())
		return true;

	s_rdc_api->StartFrameCapture(s_rdc_device, s_rdc_window);
	s_rdc_region_capturing = true;
	Msg("* [RDC] region %s capture started", s_rdc_region_label);
	return true;
}

void renderdoc_marker_end()
{
	if (--s_rdc_region_depth || !s_rdc_region_capturing)
		return;

	s_rdc_region_capturing = false;
	s_rdc_region_name[0] = 0;
	g_rdoc_marker_watch = false;

	const u32 ended = s_rdc_api->EndFrameCapture(s_rdc_device, s_rdc_window);
	if (ended)
		Msg("* [RDC] region %s capture ended, index %u", s_rdc_region_label, s_rdc_api->GetNumCaptures() - 1);
	else
		Msg("! [RDC] region %s capture ended with no file", s_rdc_region_label);
}
