#include "stdafx.h"
#pragma hdrstop

#include "renderdoc_integration.h"
#include "renderdoc_app.h"
#include "IGame_Level.h"
#include "IGame_Persistent.h"
#include "Environment.h"

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

	enum rdc_arm_mode
	{
		rdc_arm_off,
		rdc_arm_spike,
		rdc_arm_marker,
		rdc_arm_second_viewport,
	};

	rdc_arm_mode s_rdc_arm_mode = rdc_arm_off;
	wchar_t s_rdc_arm_name[64] = {};
	string64 s_rdc_arm_label = {};
	float s_rdc_arm_spike_ms = 0.0f;
	bool s_rdc_arm_capturing = false;
	bool s_rdc_arm_marker_seen = false;
	bool s_rdc_arm_viewport_prev = false;
	bool s_rdc_arm_viewport_pending = false;

	// The measured window runs from the poll in frame move through present
	u64 s_rdc_frame_begin = 0;
	u64 s_rdc_frame_frequency = 0;
	float s_rdc_frame_ms = 0.0f;

	struct rdc_frame_state
	{
		u32 frame;
		float time;
		Fvector position;
		Fvector direction;
		float fov;
		float aspect;
		u32 width;
		u32 height;
		u32 viewport;
		string_path level;
		Fvector4 shader_params[8];
		u32 shader_param_count;
		bool valid;
	};

	rdc_frame_state s_rdc_state = {};
	int s_rdc_api_major = 0;
	int s_rdc_api_minor = 0;
	int s_rdc_api_patch = 0;

	const char* const rdc_marker_names[] = {
		"CRender_Render", "render_menu", "main viewport", "second viewport", "DEFER_PART0_SPLIT",
		"static geometry", "dynamic geometry", "DEFER_TEST_LIGHT_VIS", "DEFER_PART1_SPLIT", "hud",
		"lods", "details", "DEFER_WALLMARKS", "MARK_MSAA_EDGES", "DEFER_RAIN", "DEFER_SUN",
		"sun cascade 0", "sun cascade 1", "sun cascade 2", "sun cascade", "DEFER_SELF_ILLUM", "lights",
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
		// The only actions variant does nothing until callstacks are on, so it follows them
		static const option_key keys[] = {
			{ECoreParams::rdoc_refall, eRENDERDOC_Option_RefAllResources, "reference all resources"},
			{ECoreParams::rdoc_cmdlists, eRENDERDOC_Option_CaptureAllCmdLists, "capture all command lists"},
			{ECoreParams::rdoc_callstacks, eRENDERDOC_Option_CaptureCallstacks, "capture callstacks"},
			{ECoreParams::rdoc_callstacks, eRENDERDOC_Option_CaptureCallstacksOnlyActions,
				"capture callstacks only for actions"}};

		for (const option_key& key : keys)
		{
			if (!Core.ParamsData.test(key.param))
				continue;

			s_rdc_api->SetCaptureOptionU32(key.option, 1);
			Msg("* [RDC] option %s now %u", key.name, s_rdc_api->GetCaptureOptionU32(key.option));
		}
	}

	void rdc_wait_for_ui()
	{
		if (!Core.ParamsData.test(ECoreParams::rdoc_wait))
			return;

		enum
		{
			wait_limit_ms = 60000,
			wait_step_ms = 100
		};

		Msg("* [RDC] holding startup for up to %d seconds so the replay ui can attach", wait_limit_ms / 1000);

		int waited = 0;
		while (waited < wait_limit_ms && s_rdc_api->IsTargetControlConnected() != 1)
		{
			Sleep(wait_step_ms);
			waited += wait_step_ms;
		}

		const bool connected = rdc_ui_connected();
		Msg("%s [RDC] startup resumed after %d ms, ui %s", connected ? "*" : "~", waited,
			connected ? "connected" : "never attached");
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

	void rdc_update_marker_watch()
	{
		g_rdoc_marker_watch = s_rdc_region_name[0] != 0 || s_rdc_arm_mode == rdc_arm_marker;
	}

	const char* rdc_arm_mode_name()
	{
		switch (s_rdc_arm_mode)
		{
		case rdc_arm_spike: return "spike";
		case rdc_arm_marker: return "marker";
		case rdc_arm_second_viewport: return "svp";
		default: return "off";
		}
	}

	void rdc_frame_clock_start()
	{
		if (!s_rdc_frame_frequency)
			QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&s_rdc_frame_frequency));

		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&s_rdc_frame_begin));
	}

	void rdc_frame_clock_stop()
	{
		if (!s_rdc_frame_begin || !s_rdc_frame_frequency)
			return;

		u64 now = 0;
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&now));
		s_rdc_frame_ms = float(double(now - s_rdc_frame_begin) * 1000.0 / double(s_rdc_frame_frequency));
	}

	bool rdc_arm_ready()
	{
		if (!rdc_available())
			return false;

		if (s_rdc_version < eRENDERDOC_API_Version_1_4_0)
		{
			Msg("! [RDC] host api is older than 1.4.0, an armed capture cannot discard a frame");
			return false;
		}

		if (!s_rdc_device || !s_rdc_window)
		{
			Msg("! [RDC] the renderer has no device yet, an armed capture needs a running frame");
			return false;
		}

		return true;
	}

	void rdc_arm_clear(const char* reason)
	{
		s_rdc_arm_mode = rdc_arm_off;
		s_rdc_arm_name[0] = 0;
		s_rdc_arm_capturing = false;
		s_rdc_arm_viewport_pending = false;
		rdc_update_marker_watch();
		Msg("* [RDC] frame arm disarmed, %s", reason);
	}

	void rdc_arm_report()
	{
		s_rdc_arm_viewport_prev = Device.m_SecondViewport.IsSVPActive();
		s_rdc_arm_viewport_pending = false;
		rdc_update_marker_watch();
		Msg("~ [RDC] every frame is now recorded until the condition hits, expect a much lower frame rate");
	}

	bool rdc_arm_condition(bool viewport)
	{
		switch (s_rdc_arm_mode)
		{
		case rdc_arm_spike:
			return s_rdc_frame_ms > s_rdc_arm_spike_ms;

		case rdc_arm_marker:
			return s_rdc_arm_marker_seen;

		case rdc_arm_second_viewport:
			return viewport && s_rdc_arm_viewport_pending;

		default:
			return false;
		}
	}

	void rdc_record_state(const Fvector4* shader_params, u32 count)
	{
		s_rdc_state.frame = Device.dwFrame;
		s_rdc_state.time = Device.fTimeGlobal;
		s_rdc_state.position = Device.vCameraPosition;
		s_rdc_state.direction = Device.vCameraDirection;
		s_rdc_state.fov = Device.fFOV;
		s_rdc_state.aspect = Device.fASPECT;
		s_rdc_state.width = Device.dwWidth;
		s_rdc_state.height = Device.dwHeight;
		s_rdc_state.viewport = Device.m_SecondViewport.IsSVPFrame() ? 1u : 0u;
		xr_strcpy(s_rdc_state.level, g_pGameLevel ? g_pGameLevel->name().c_str() : "");

		s_rdc_state.shader_param_count = 0;
		for (u32 index = 0; index < count && shader_params; ++index)
		{
			if (s_rdc_state.shader_param_count >= std::size(s_rdc_state.shader_params))
				break;

			s_rdc_state.shader_params[s_rdc_state.shader_param_count++] = shader_params[index];
		}

		s_rdc_state.valid = true;
	}

	// SetCaptureTitle needs a running capture and lands on the next one that ends
	void rdc_set_capture_title()
	{
		if (s_rdc_version < eRENDERDOC_API_Version_1_6_0 || !g_pGamePersistent)
			return;

		CEnvironment& environment = g_pGamePersistent->Environment();
		const float game_time = environment.GetGameTime();
		const int hours = iFloor(game_time / 3600.0f) % 24;
		const int minutes = iFloor(game_time / 60.0f) % 60;

		string256 title = {};
		xr_sprintf(title, "%s  %s  %02d:%02d  %.1f %.1f %.1f",
			s_rdc_state.level[0] ? s_rdc_state.level : "no level",
			environment.GetWeather().c_str() ? environment.GetWeather().c_str() : "no weather",
			hours, minutes,
			s_rdc_state.position.x, s_rdc_state.position.y, s_rdc_state.position.z);

		s_rdc_api->SetCaptureTitle(title);
	}

	xr_string rdc_escape_json(const char* text)
	{
		xr_string escaped;
		for (const char* cursor = text; *cursor; ++cursor)
		{
			if (*cursor == '\\' || *cursor == '"')
				escaped += '\\';
			escaped += *cursor;
		}

		return escaped;
	}

	// The engine writer lowercases and clamps a path, the capture path has to survive both
	void rdc_write_sidecar(const char* path, u64 timestamp)
	{
		if (!s_rdc_state.valid)
			return;

		wchar_t sidecar[MAX_PATH * 4] = {};
		const int widened =
			MultiByteToWideChar(CP_UTF8, 0, path, -1, sidecar, int(std::size(sidecar)) - 8);

		FILE* file = nullptr;
		if (!widened || wcscat_s(sidecar, L".json") || _wfopen_s(&file, sidecar, L"wb") || !file)
		{
			Msg("~ [RDC] sidecar for %s could not be written", path);
			return;
		}

		fprintf(file, "{\n");
		fprintf(file, "  \"capture\": \"%s\",\n", rdc_escape_json(path).c_str());
		fprintf(file, "  \"timestamp\": %llu,\n", timestamp);
		fprintf(file, "  \"api\": \"%d.%d.%d\",\n", s_rdc_api_major, s_rdc_api_minor, s_rdc_api_patch);
		fprintf(file, "  \"frame\": %u,\n", s_rdc_state.frame);
		fprintf(file, "  \"time\": %f,\n", s_rdc_state.time);
		fprintf(file, "  \"camera_position\": [%f, %f, %f],\n",
			s_rdc_state.position.x, s_rdc_state.position.y, s_rdc_state.position.z);
		fprintf(file, "  \"camera_direction\": [%f, %f, %f],\n",
			s_rdc_state.direction.x, s_rdc_state.direction.y, s_rdc_state.direction.z);
		fprintf(file, "  \"camera_fov\": %f,\n", s_rdc_state.fov);
		fprintf(file, "  \"camera_aspect\": %f,\n", s_rdc_state.aspect);
		fprintf(file, "  \"render_width\": %u,\n", s_rdc_state.width);
		fprintf(file, "  \"render_height\": %u,\n", s_rdc_state.height);
		fprintf(file, "  \"level\": \"%s\",\n", rdc_escape_json(s_rdc_state.level).c_str());
		fprintf(file, "  \"svp\": %u,\n", s_rdc_state.viewport);
		fprintf(file, "  \"shader_param\": [\n");

		for (u32 index = 0; index < s_rdc_state.shader_param_count; ++index)
		{
			const Fvector4& value = s_rdc_state.shader_params[index];
			fprintf(file, "    [%f, %f, %f, %f]%s\n", value.x, value.y, value.z, value.w,
				index + 1 < s_rdc_state.shader_param_count ? "," : "");
		}

		fprintf(file, "  ]\n}\n");
		fclose(file);

		s_rdc_state.valid = false;
	}

	bool rdc_capture_path(u32 index, xr_string& path, u64& timestamp)
	{
		u32 length = 0;
		if (!s_rdc_api->GetCapture(index, nullptr, &length, &timestamp) || !length)
			return false;

		xr_vector<char> buffer(size_t(length) + 1, 0);
		if (!s_rdc_api->GetCapture(index, buffer.data(), &length, &timestamp))
			return false;

		path = buffer.data();
		return true;
	}

	void rdc_log_capture(u32 index)
	{
		xr_string path;
		u64 timestamp = 0;
		if (!rdc_capture_path(index, path, timestamp))
			return;

		s_rdc_latest_capture = path;
		rdc_write_sidecar(path.c_str(), timestamp);
		Msg("* [RDC] capture written %s  timestamp %llu", path.c_str(), timestamp);
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

	rdc_wait_for_ui();

	// The engine keeps its own crash handler and minidump pipeline
	s_rdc_api->UnloadCrashHandler();

	s_rdc_api->SetCaptureKeys(nullptr, 0);
	Msg("* [RDC] capture hotkeys disabled, F12 stays the engine screenshot and rdoc_capture is the trigger");

	rdc_apply_capture_options();
	rdc_prepare_capture_path();
	s_rdc_seen_captures = s_rdc_api->GetNumCaptures();

	s_rdc_api->GetAPIVersion(&s_rdc_api_major, &s_rdc_api_minor, &s_rdc_api_patch);
	Msg("* [RDC] capture API %d.%d.%d ready, ui %s, annotations %s",
		s_rdc_api_major, s_rdc_api_minor, s_rdc_api_patch,
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

	rdc_frame_clock_start();

	const u32 count = s_rdc_api->GetNumCaptures();
	for (; s_rdc_seen_captures < count; ++s_rdc_seen_captures)
		rdc_log_capture(s_rdc_seen_captures);
}

void renderdoc_open_replay_ui(int index)
{
	if (!rdc_available())
		return;

	xr_string wanted = s_rdc_latest_capture;
	if (index >= 0)
	{
		u64 timestamp = 0;
		if (!rdc_capture_path(u32(index), wanted, timestamp))
		{
			Msg("! [RDC] capture index %d does not exist", index);
			return;
		}
	}

	if (index < 0 && rdc_ui_connected() && s_rdc_version >= eRENDERDOC_API_Version_1_5_0)
	{
		const u32 shown = s_rdc_api->ShowReplayUI();
		Msg("%s [RDC] connected ui raise %s", shown ? "*" : "~", shown ? "accepted" : "refused");
		return;
	}

	// The path rides a command line so quotes keep a spaced folder in one argument
	const xr_string quoted = wanted.empty() ? xr_string() : xr_string("\"" + wanted + "\"");
	const char* const capture = quoted.empty() ? nullptr : quoted.c_str();
	const u32 pid = s_rdc_api->LaunchReplayUI(1, capture);
	if (pid)
		Msg("* [RDC] replay ui launched pid %u %s", pid, capture ? capture : "with no capture");
	else
		Msg("! [RDC] replay ui launch failed");
}

void renderdoc_status()
{
	if (!rdc_available())
		return;

	Msg("* [RDC] api %d.%d.%d, overlay %s, captures %u, ui %s",
		s_rdc_api_major, s_rdc_api_minor, s_rdc_api_patch,
		(s_rdc_api->GetOverlayBits() & eRENDERDOC_Overlay_Enabled) ? "on" : "off",
		s_rdc_api->GetNumCaptures(), rdc_ui_connected() ? "connected" : "not connected");

	Msg("* [RDC] path template %s", s_rdc_api->GetCaptureFilePathTemplate());

	Msg("* [RDC] callstacks %u, only actions %u, reference all resources %u, all command lists %u",
		s_rdc_api->GetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks),
		s_rdc_api->GetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacksOnlyActions),
		s_rdc_api->GetCaptureOptionU32(eRENDERDOC_Option_RefAllResources),
		s_rdc_api->GetCaptureOptionU32(eRENDERDOC_Option_CaptureAllCmdLists));

	string128 arm = {};
	switch (s_rdc_arm_mode)
	{
	case rdc_arm_spike: xr_sprintf(arm, "spike over %.2f ms", s_rdc_arm_spike_ms); break;
	case rdc_arm_marker: xr_sprintf(arm, "marker %s", s_rdc_arm_label); break;
	default: xr_strcpy(arm, rdc_arm_mode_name()); break;
	}

	Msg("* [RDC] arm %s, armed capture %s, pending region %s, last frame %.2f ms", arm,
		s_rdc_arm_capturing ? "running" : "idle",
		s_rdc_region_name[0] ? s_rdc_region_label : "none", s_rdc_frame_ms);

	Msg("* [RDC] rdoc_capture cannot start while armed because a capture is always in progress");
}

void renderdoc_list_captures()
{
	if (!rdc_available())
		return;

	const u32 count = s_rdc_api->GetNumCaptures();
	if (!count)
	{
		Msg("* [RDC] no captures yet");
		return;
	}

	// A capture deleted in the ui keeps its slot, so a path here may already be gone
	for (u32 index = 0; index < count; ++index)
	{
		xr_string path;
		u64 timestamp = 0;
		if (rdc_capture_path(index, path, timestamp))
			Msg("  %u  %llu  %s", index, timestamp, path.c_str());
	}
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
	if (!s_rdc_api)
		return;

	// A region capture opens later in the frame so an armed marker records the state up front
	const bool capturing = s_rdc_api->IsFrameCapturing() == 1;
	if (!capturing && !g_rdoc_marker_watch)
		return;

	rdc_record_state(shader_params, count);

	if (!capturing)
		return;

	rdc_set_capture_title();

	if (!s_rdc_annotations)
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
	rdc_update_marker_watch();
	Msg("* [RDC] armed on marker %s, the next outermost region writes one capture", marker);
}

bool renderdoc_marker_begin(const wchar_t* name)
{
	if (s_rdc_arm_mode == rdc_arm_marker && !_wcsicmp(name, s_rdc_arm_name))
		s_rdc_arm_marker_seen = true;

	if (!s_rdc_region_name[0] || _wcsicmp(name, s_rdc_region_name))
		return false;

	// Only the outermost instance opens, the inner ones just carry the depth
	if (s_rdc_region_depth++ || s_rdc_api->IsFrameCapturing())
		return true;

	s_rdc_api->StartFrameCapture(s_rdc_device, s_rdc_window);
	s_rdc_region_capturing = true;
	rdc_set_capture_title();
	Msg("* [RDC] region %s capture started", s_rdc_region_label);
	return true;
}

void renderdoc_marker_end()
{
	if (--s_rdc_region_depth || !s_rdc_region_capturing)
		return;

	s_rdc_region_capturing = false;
	s_rdc_region_name[0] = 0;
	rdc_update_marker_watch();

	const u32 ended = s_rdc_api->EndFrameCapture(s_rdc_device, s_rdc_window);
	if (ended)
		Msg("* [RDC] region %s capture ended, index %u", s_rdc_region_label, s_rdc_api->GetNumCaptures() - 1);
	else
		Msg("! [RDC] region %s capture ended with no file", s_rdc_region_label);
}

void renderdoc_arm_spike(float milliseconds)
{
	if (!rdc_arm_ready())
		return;

	s_rdc_arm_mode = rdc_arm_spike;
	s_rdc_arm_spike_ms = milliseconds;
	rdc_arm_report();
	Msg("* [RDC] armed on a frame longer than %.2f ms measured from frame move through present", milliseconds);
}

void renderdoc_arm_marker(const char* name)
{
	if (!rdc_arm_ready())
		return;

	if (!rdc_widen(name, s_rdc_arm_name, std::size(s_rdc_arm_name)))
	{
		s_rdc_arm_name[0] = 0;
		Msg("! [RDC] marker name %s does not fit", name);
		return;
	}

	xr_strcpy(s_rdc_arm_label, name);
	s_rdc_arm_mode = rdc_arm_marker;
	rdc_arm_report();
	Msg("* [RDC] armed on a frame that reaches marker %s", name);
}

void renderdoc_arm_second_viewport()
{
	if (!rdc_arm_ready())
		return;

	s_rdc_arm_mode = rdc_arm_second_viewport;
	rdc_arm_report();
	Msg("* [RDC] armed on the first rendered viewport frame after the second viewport turns on");
}

void renderdoc_disarm()
{
	if (s_rdc_arm_mode == rdc_arm_off)
	{
		Msg("* [RDC] no frame arm is set");
		return;
	}

	if (s_rdc_arm_capturing)
		s_rdc_api->DiscardFrameCapture(s_rdc_device, s_rdc_window);

	rdc_arm_clear("by request");
}

void renderdoc_frame_begin()
{
	if (s_rdc_arm_mode == rdc_arm_off)
		return;

	s_rdc_arm_marker_seen = false;

	// Overlapping captures are undefined so a region capture keeps this one out
	if (s_rdc_api->IsFrameCapturing())
		return;

	s_rdc_api->StartFrameCapture(s_rdc_device, s_rdc_window);
	s_rdc_arm_capturing = true;
}

void renderdoc_frame_end()
{
	rdc_frame_clock_stop();

	if (s_rdc_arm_mode == rdc_arm_off)
		return;

	// The viewport turning on is the edge, the first frame it renders after that is the catch
	const bool active = Device.m_SecondViewport.IsSVPActive();
	if (active && !s_rdc_arm_viewport_prev)
		s_rdc_arm_viewport_pending = true;
	s_rdc_arm_viewport_prev = active;

	const bool keep = rdc_arm_condition(Device.m_SecondViewport.IsSVPFrame());

	if (!s_rdc_arm_capturing)
		return;

	s_rdc_arm_capturing = false;
	if (!keep)
	{
		s_rdc_api->DiscardFrameCapture(s_rdc_device, s_rdc_window);
		return;
	}

	const u32 ended = s_rdc_api->EndFrameCapture(s_rdc_device, s_rdc_window);
	if (ended)
		Msg("* [RDC] frame kept on %s, index %u", rdc_arm_mode_name(), s_rdc_api->GetNumCaptures() - 1);
	else
		Msg("! [RDC] frame kept on %s ended with no file", rdc_arm_mode_name());

	rdc_arm_clear("the condition hit");
}
