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
#define HAS_STREAMLINE 0
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
    void SL_DLSS_Evaluate(); // CRenderTarget::phase_combine (ps_ssfx_upscaler == 2) -- the upscale pass
    void SL_Shutdown();      // CRenderDevice::Destroy -- slShutdown

    // Per-frame helpers so xrEngine hook sites stay SDK-free (plan Addendum A4):
    void SL_NewFrameToken(); // CRenderDevice::on_idle, top -- slGetNewFrameToken
    void SL_PCLMarker(int marker); // slPCLSetMarker(marker, Device.PCL_currentFrame); marker = sl::PCLMarker int
    void SL_ReflexSleep();   // CRenderDevice::on_idle -- slReflexSleep

    // Recompute Current_RenderScale + Real_* from ps_r_upscaler_qual_token (plan Addendum C2).
    // Safe to call without the SDK; only touches Device fields (which exist once HAS_STREAMLINE prerequisites
    // are met). Called from SL_Init and whenever the resolution token changes (before a vid_restart).
    void UpdateRenderScale();
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
