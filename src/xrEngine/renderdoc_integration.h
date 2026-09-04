#pragma once

ENGINE_API void renderdoc_initialize();
ENGINE_API bool renderdoc_api_live();
ENGINE_API void renderdoc_poll_captures();
ENGINE_API void renderdoc_trigger_capture(u32 frames);
ENGINE_API void renderdoc_open_replay_ui();
ENGINE_API void renderdoc_set_overlay(bool visible);
ENGINE_API bool renderdoc_overlay_enabled();
ENGINE_API void renderdoc_set_active_window(void* device, void* window);

ENGINE_API void renderdoc_annotate_frame(const Fvector4* shader_params, u32 count);

ENGINE_API extern bool g_rdoc_marker_watch;
ENGINE_API void renderdoc_capture_region(const char* marker);
ENGINE_API bool renderdoc_marker_begin(const wchar_t* name);
ENGINE_API void renderdoc_marker_end();

ENGINE_API void renderdoc_arm_spike(float milliseconds);
ENGINE_API void renderdoc_arm_marker(const char* name);
ENGINE_API void renderdoc_arm_second_viewport();
ENGINE_API void renderdoc_disarm();
ENGINE_API void renderdoc_frame_begin();
ENGINE_API void renderdoc_frame_end();
