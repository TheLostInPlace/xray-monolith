// StreamlineWrapper.h -- NVIDIA Streamline (DLSS Super Resolution + Reflex + PCL) integration.
//
// Ported from the "Screen Space Shaders UPDATE 24" engine. See the full reverse-engineering spec at
// docs/streamline_dlss_integration_plan.md (in the companion repo) -- every value here is binary-confirmed.
//
// ----------------------------------------------------------------------------------------------------
// MASTER SWITCH
// ----------------------------------------------------------------------------------------------------
// HAS_STREAMLINE stays 0 until ALL of the following are in place, after which set it to 1:
//   1. NVIDIA Streamline SDK 2.10 headers on the include path (sl.h, sl_consts.h, sl_reflex.h,
//      sl_dlss.h, sl_pcl.h) and sl.interposer.lib added to the linker (see plan Section 7).
//   2. The CRenderDeviceData fields added (plan Addendum B): Target_Width/Height, Real_Width/Height,
//      Current_RenderScale, PCL_currentFrame (sl::FrameToken*), slDoPCL, HaltonJittering[72].
//   3. rt_sceneAA created at display resolution (plan Section 6.4).
// While HAS_STREAMLINE == 0 every method below compiles to an empty no-op, so the engine builds and runs
// exactly as before -- the console variables still register, DLSS simply does nothing.
//
// The public interface is intentionally SDK-free (no sl:: types) so xrEngine and the render layer can both
// call g_SLWrapper without pulling in the Streamline headers. All sl:: usage is confined to the .cpp.
// ----------------------------------------------------------------------------------------------------

#pragma once

#ifndef HAS_STREAMLINE
#define HAS_STREAMLINE 1 // NV Streamline active (SDK 2.10.3 in src/3rd party/Streamline). Set 0 to disable.
#endif

// Sub-native rendering: scene render targets are ALLOCATED at Real_* size (the SSS 24 architecture,
// confirmed from the binary CRenderTarget ctor: G-buffer/generic/blur/mvec/position at Real_*;
// rt_sceneAA/rt_sceneFinal/rt_tempzb at Target_*). Resolution changes require vid_restart.
// Currently 0 (DLAA only): the SSS 24 gamedata shader chain (scene_aa -> combine_2 -> scene_final ->
// postprocess.s) is wired for equal sizes; sub-native additionally needs the post-upscale tail to run at
// Target_* on the scene_aa/scene_final surfaces, which is the next milestone.
#ifndef SL_SUBNATIVE
#define SL_SUBNATIVE 0
#endif

class SLWrapper
{
public:
    bool m_SLInit      = false; // slInit + slSetD3DDevice succeeded
    bool m_bDlssInit   = false; // DLSS feature supported + options set; SL_DLSS_Evaluate gates on this
    bool m_bReflexInit = false; // Reflex low-latency available + options set

    // Lifecycle (hook sites in the plan doc):
    void SL_Init();          // CRender::create   -- slInit, slSetD3DDevice, DLSS support, then Reflex
    void SL_DLSS_Init();     // CRender::create / on preset change -- slDLSSSetOptions (Update_DLSSOptions)
    void SL_Reflex_Init();   // CRender::create / on ssfx_reflex change -- Reflex support + slReflexSetOptions
    bool SL_DLSS_Evaluate(); // CRenderTarget::phase_combine (ps_ssfx_upscaler == 2); true if the upscale ran
    void SL_Shutdown();      // CRenderDevice::Destroy -- slShutdown

    // Per-frame helpers so xrEngine hook sites stay SDK-free (plan Addendum A4):
    void SL_NewFrameToken(); // CRenderDevice::on_idle, top -- slGetNewFrameToken
    void SL_PCLMarker(int marker); // slPCLSetMarker(marker, Device.PCL_currentFrame); marker = sl::PCLMarker int
    void SL_ReflexSleep();   // CRenderDevice::on_idle -- slReflexSleep

    // Capture Target_* from the current video mode and compute Real_* -- MUST run before CRenderTarget is
    // constructed (CRender::create and reset_end), because the scene RTs are allocated at Real_* size.
    void SL_SetupResolution();

    // Recompute Current_RenderScale + Real_* from ps_r_upscaler_qual_token (plan Addendum C2).
    // Safe to call without the SDK; only touches Device fields (which exist once HAS_STREAMLINE prerequisites
    // are met). Called from SL_Init and whenever the resolution token changes (before a vid_restart).
    void UpdateRenderScale();

    // Dynamic resolution: render the scene at Real_* (Begin) then restore Target_* (End) before the upscale.
    // No-op unless HAS_STREAMLINE and ps_ssfx_upscaler == 2 (DLSS). See r4_R_render.cpp / phase_combine.
    void BeginSceneResolution();
    void EndSceneResolution();

    // Texture mip LOD bias = log2(Real/Target) when upscaling (ssfx_upscaler_automipmap); routed through
    // the engine's existing r__tf_mipbias command so samplers update live.
    void ApplyAutoMipBias();
};

extern SLWrapper g_SLWrapper;

// sl::PCLMarker integer values (plan Addendum A4) -- exposed so xrEngine hook sites can pass them without
// including the SDK. These match sl_pcl.h exactly.
enum
{
    SL_PCL_eSimulationStart   = 0,
    SL_PCL_eSimulationEnd     = 1,
    SL_PCL_eRenderSubmitStart = 2,
    SL_PCL_eRenderSubmitEnd   = 3,
    SL_PCL_ePresentStart      = 4,
    SL_PCL_ePresentEnd        = 5,
    SL_PCL_eInputSample       = 6,
    SL_PCL_eTriggerFlash      = 7,
    SL_PCL_ePCLatencyPing     = 8,
};
