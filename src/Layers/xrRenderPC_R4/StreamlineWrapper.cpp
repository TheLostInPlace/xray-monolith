// StreamlineWrapper.cpp -- see StreamlineWrapper.h for the HAS_STREAMLINE master switch.
//
// While HAS_STREAMLINE == 0 every method is an empty no-op, so this file compiles and links with no SDK and
// no new engine fields present. Set HAS_STREAMLINE to 1 only after the prerequisites in the header are met.
// All numeric values below are binary-confirmed (docs/streamline_dlss_integration_plan.md, Addenda A-D).

#include "stdafx.h"
#include "StreamlineWrapper.h"
#include "../../xrEngine/device.h"          // CRenderDevice Device (SDK-free)
#include "../xrRender/xrRender_console.h"   // ps_ssfx_upscaler, ps_r_upscaler_qual_token, ...

#if HAS_STREAMLINE
#include <sl.h>
#include <sl_consts.h>
#include <sl_reflex.h>
#include <sl_dlss.h>
#include <sl_pcl.h>
#include <sl_helpers.h>   // sl::getResultAsStr

#include "../xrRender/HW.h"
#include "r4.h"
#include "r4_rendertarget.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/xr_ioconsole.h" // Console->Execute for r__tf_mipbias

// File-scope viewport handle (in the original binary this is a global, not a member).
static sl::ViewportHandle g_sl_viewport{ 0 };

// Device.PCL_currentFrame is stored as void* (so xrEngine stays free of the SL SDK); cast it back here.
static inline sl::FrameToken* SL_Frame() { return reinterpret_cast<sl::FrameToken*>(Device.PCL_currentFrame); }

// ---- console live re-apply hooks (g_sl_on_* in xrRender_console) -------------------------------------
// Values the current render targets were built with; seeded by SL_SetupResolution (create + reset_end).
// The hook only reacts when a console execute actually CHANGES them -- the SSS MCM script re-executes the
// upscaler commands on every game start, and an unconditional vid_restart caused gratuitous double device
// resets mid-load. (Binary: CCC_Upscaler_Qual::Execute restarts only when the token changed.)
int g_sl_built_upscaler   = -1;
int g_sl_built_resolution = -1;
int g_sl_built_automip    = -1;

static void sl_hook_upscaler_change()
{
    if (!HW.pDevice) return; // before device creation CRender::create handles everything

    const bool changed = (ps_ssfx_upscaler != g_sl_built_upscaler) ||
                         (ps_r_upscaler_qual_token != g_sl_built_resolution) ||
                         (ps_ssfx_upscaler_automipmap != g_sl_built_automip);
    if (!changed)
        return;

    if (ps_ssfx_upscaler == 2 && !g_SLWrapper.m_SLInit)
    {
        // Streamline failed to initialize at launch (it now always initializes in CRender::create, like
        // the binary). Mid-session slInit deadlocks the device, so this is the unhappy-path fallback only.
        Msg("! UPSCALING : Streamline is not initialized -- DLSS requires a game restart.");
        return;
    }
    if (ps_ssfx_upscaler == 0)
    {
        Device.Real_Width  = Device.Target_Width;
        Device.Real_Height = Device.Target_Height;
        Device.Current_RenderScale = 1.0f;
    }
    else
        g_SLWrapper.UpdateRenderScale();
    if (g_SLWrapper.m_bDlssInit)
        g_SLWrapper.SL_DLSS_Init(); // re-apply mode/output options
    g_SLWrapper.ApplyAutoMipBias();
#if SL_SUBNATIVE
    // Scene RTs are allocated at Real_*, so a real change must recreate them (binary behavior).
    // reset_end re-runs SL_SetupResolution, which re-seeds g_sl_built_* and stops restart loops.
    if (Console)
        Console->Execute("vid_restart");
#endif
}
static void sl_hook_preset_change()
{
    if (g_SLWrapper.m_bDlssInit)
        g_SLWrapper.SL_DLSS_Init(); // binary: CCC_DlssPreset_Mode -> Update_DLSSOptions
}
static void sl_hook_reflex_change()
{
    if (g_SLWrapper.m_SLInit)
        g_SLWrapper.SL_Reflex_Init(); // binary: CCC_NvReflex -> slReflexSetOptions live
}
static struct SLConsoleHookInit
{
    SLConsoleHookInit()
    {
        g_sl_on_upscaler_change = sl_hook_upscaler_change;
        g_sl_on_preset_change   = sl_hook_preset_change;
        g_sl_on_reflex_change   = sl_hook_reflex_change;
    }
} s_sl_console_hook_init;
#endif // HAS_STREAMLINE

SLWrapper g_SLWrapper;

// ----------------------------------------------------------------------------------------------------
void SLWrapper::UpdateRenderScale()
{
    // Render-scale ratios per quality token (plan Addendum C2). SDK-free -- only touches Device fields.
    float scale = 1.0f;
#if SL_SUBNATIVE
    if (ps_ssfx_upscaler == 2) // sub-native only for DLSS (FSR not ported); off/fsr render 1:1
    {
        switch (ps_r_upscaler_qual_token)
        {
            case 2:  scale = 0.66f; break; // Quality
            case 3:  scale = 0.58f; break; // Balanced
            case 4:  scale = 0.50f; break; // Performance
            case 5:  scale = 0.33f; break; // Ultra Performance
            default: scale = 1.00f; break; // DLAA / NativeAA (token 1)
        }
    }
#endif
    Device.Current_RenderScale = scale;
    Device.Real_Width  = (u32)(Device.Target_Width  * scale + 0.5f);
    Device.Real_Height = (u32)(Device.Target_Height * scale + 0.5f);
}

// ----------------------------------------------------------------------------------------------------
void SLWrapper::SL_SetupResolution()
{
    Device.Target_Width  = Device.dwWidth;  // current video mode = output resolution
    Device.Target_Height = Device.dwHeight;
    UpdateRenderScale();
#if HAS_STREAMLINE
    // Seed the change-detection baseline: the RTs about to be (re)built use these values.
    extern int g_sl_built_upscaler, g_sl_built_resolution, g_sl_built_automip;
    g_sl_built_upscaler   = ps_ssfx_upscaler;
    g_sl_built_resolution = ps_r_upscaler_qual_token;
    g_sl_built_automip    = ps_ssfx_upscaler_automipmap;
#endif
    // Always log the full state -- this is the ground truth for diagnosing render-path issues.
    Msg("- UPSCALING : setup [ upscaler=%d resolution_token=%d preset=%d ] render %ux%u -> output %ux%u [ %.0f%% ]",
        ps_ssfx_upscaler, ps_r_upscaler_qual_token, ps_r_dlsspreset_token,
        Device.Real_Width, Device.Real_Height, Device.Target_Width, Device.Target_Height,
        Device.Current_RenderScale * 100.0f);
}

// ----------------------------------------------------------------------------------------------------
// Dynamic resolution toggle. The scene + pre-upscale post render into the Real_* sub-rect of the full-size
// RTs; phase_combine restores Target_* before the DLSS output. DLAA (Real_* == Target_*) makes both no-ops.
void SLWrapper::BeginSceneResolution()
{
#if HAS_STREAMLINE
    if (m_bDlssInit && ps_ssfx_upscaler == 2)
    {
        Device.dwWidth  = Device.Real_Width;
        Device.dwHeight = Device.Real_Height;
    }
#endif
}

void SLWrapper::EndSceneResolution()
{
#if HAS_STREAMLINE
    if (m_bDlssInit && ps_ssfx_upscaler == 2)
    {
        Device.dwWidth  = Device.Target_Width;
        Device.dwHeight = Device.Target_Height;
    }
#endif
}

// ----------------------------------------------------------------------------------------------------
void SLWrapper::SL_Init()
{
#if HAS_STREAMLINE
    // Initialize Streamline at launch UNCONDITIONALLY (binary model: its gate is the shader-support
    // option bit, not the cvar -- slInit always runs when the SSS shaders are installed). This is what
    // makes live off/dlss switching possible: by the time the user toggles, everything is already
    // initialized and only the options need re-applying. Evaluate itself only runs when upscaler == 2.
    UpdateRenderScale();

    sl::Preferences pref{};
    static const sl::Feature feats[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };
    pref.featuresToLoad    = feats;
    pref.numFeaturesToLoad = _countof(feats);
    pref.applicationId     = 231313132; // binary-confirmed
    pref.renderAPI         = sl::RenderAPI::eD3D11;
    // REQUIRED for slSetTagForFrame (without it DLSS gets no resources -> eErrorMissingInputParameter).
    // Matches the binary: Preferences flags field == 128 == 1<<7.
    pref.flags             = sl::PreferenceFlags::eUseFrameBasedResourceTagging;

    sl::Result r = slInit(pref, sl::kSDKVersion); // SDK 2.10
    if (r != sl::Result::eOk)
        Msg("! NV Streamline : Initialization failed [ %s ]", sl::getResultAsStr(r));
    else
        Msg("- NV Streamline : SDK 2.10 Initialized");

    if (slSetD3DDevice(HW.pDevice) != sl::Result::eOk)
        Msg("! NV Streamline : Failed to set D3DDevice");
    else
        m_SLInit = true;

    // DLSS support check.
    if (!m_bDlssInit && HW.m_pAdapter)
    {
        DXGI_ADAPTER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        if (SUCCEEDED(HW.m_pAdapter->GetDesc(&desc)))
        {
            sl::AdapterInfo adapter{};
            adapter.deviceLUID            = (uint8_t*)&desc.AdapterLuid;
            adapter.deviceLUIDSizeInBytes = sizeof(desc.AdapterLuid);

            if (slIsFeatureSupported(sl::kFeatureDLSS, adapter) == sl::Result::eOk)
            {
                SL_DLSS_Init();
                m_bDlssInit = true;
                Msg("- NV Streamline : DLSS is available.");
            }
            else
            {
                Msg("! NV Streamline : DLSS is not supported");
            }
        }
    }

    SL_Reflex_Init();
    Device.slDoPCL = m_SLInit; // enable PCL latency markers once Streamline is live
    ApplyAutoMipBias();
    // FSR3Wrapper::InitFSR3();  // separate AMD path, intentionally not ported here.
#endif
}

// ----------------------------------------------------------------------------------------------------
void SLWrapper::ApplyAutoMipBias()
{
#if HAS_STREAMLINE
    if (!ps_ssfx_upscaler_automipmap || !Console)
        return;
    float bias = 0.0f;
    if (ps_ssfx_upscaler && Device.Target_Width && Device.Real_Width < Device.Target_Width)
        bias = log2f((float)Device.Real_Width / (float)Device.Target_Width); // negative when sub-native
    string128 cmd;
    xr_sprintf(cmd, "r__tf_mipbias %.2f", bias);
    Console->Execute(cmd); // CCC_tf_MipBias applies it to the samplers live
    Msg("- UPSCALING : Auto Mip-Map Bias [ %f ]", bias);
#endif
}

// ----------------------------------------------------------------------------------------------------
void SLWrapper::SL_DLSS_Init() // == binary SLWrapper::Update_DLSSOptions (plan Addendum A3)
{
#if HAS_STREAMLINE
    sl::DLSSOptions opt{};
    opt.dlaaPreset             = (sl::DLSSPreset)ps_r_dlsspreset_token;
    opt.qualityPreset          = (sl::DLSSPreset)ps_r_dlsspreset_token;
    opt.balancedPreset         = (sl::DLSSPreset)ps_r_dlsspreset_token;
    opt.performancePreset      = (sl::DLSSPreset)ps_r_dlsspreset_token;
    opt.ultraPerformancePreset = (sl::DLSSPreset)ps_r_dlsspreset_token;
    opt.ultraQualityPreset     = (sl::DLSSPreset)ps_r_dlsspreset_token;

    switch (ps_r_upscaler_qual_token)
    {
        case 2:  opt.mode = sl::DLSSMode::eMaxQuality;       break;
        case 3:  opt.mode = sl::DLSSMode::eBalanced;         break;
        case 4:  opt.mode = sl::DLSSMode::eMaxPerformance;   break;
        case 5:  opt.mode = sl::DLSSMode::eUltraPerformance; break;
        default: opt.mode = sl::DLSSMode::eDLAA;             break;
    }
    // While the render scale is clamped to 1.0 (SL_SUBNATIVE == 0), input == output resolution, which is
    // by definition DLAA -- report it as such or DLSS may reject the mode/extent combination.
    if (Device.Real_Width == Device.Target_Width && Device.Real_Height == Device.Target_Height)
        opt.mode = sl::DLSSMode::eDLAA;
    opt.outputWidth     = Device.Target_Width;
    opt.outputHeight    = Device.Target_Height;
    opt.colorBuffersHDR = ps_r4_hdr10_on ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    opt.sharpness       = 0.0f;
    opt.preExposure     = 1.0f;
    opt.exposureScale   = 1.0f;
    // BINARY-EXACT (Update_DLSSOptions dump): useAutoExposure = true. Our input is the same post-lut
    // rt_Generic_0 the binary feeds; match it.
    opt.useAutoExposure = sl::Boolean::eTrue;

    sl::Result r = slDLSSSetOptions(g_sl_viewport, opt);
    if (r != sl::Result::eOk)
        Msg("! NV Streamline : Failed to set DLSS options [ %s ]", sl::getResultAsStr(r));
#endif
}

// ----------------------------------------------------------------------------------------------------
void SLWrapper::SL_Reflex_Init() // plan Section 3 (corrected)
{
#if HAS_STREAMLINE
    if (!HW.m_pAdapter) return;
    DXGI_ADAPTER_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    if (FAILED(HW.m_pAdapter->GetDesc(&desc))) return;

    sl::AdapterInfo adapter{};
    adapter.deviceLUID            = (uint8_t*)&desc.AdapterLuid;
    adapter.deviceLUIDSizeInBytes = sizeof(desc.AdapterLuid);

    if (slIsFeatureSupported(sl::kFeatureReflex, adapter) != sl::Result::eOk)
    {
        Msg("! NV Streamline : Reflex is not supported");
        m_bReflexInit = false;
        return;
    }

    sl::ReflexState state{};
    if (slReflexGetState(state) != sl::Result::eOk)
    {
        Msg("! NV Streamline : Reflex can't get the current state");
        m_bReflexInit = false;
        return;
    }
    if (!state.lowLatencyAvailable)
    {
        m_bReflexInit = false;
        return;
    }

    m_bReflexInit = true;
    Msg("- NV Streamline : Reflex low Latency available.");

    sl::ReflexOptions ro{};
    ro.mode                 = sl::ReflexMode::eOff;
    ro.useMarkersToOptimize = true;
    ro.frameLimitUs         = 0;
    if (ps_ssfx_reflex == 1)
        ro.mode = sl::ReflexMode::eLowLatency;
    else if (ps_ssfx_reflex == 2)
        ro.mode = sl::ReflexMode::eLowLatencyWithBoost;

    if (slReflexSetOptions(ro) != sl::Result::eOk)
        Msg("- NV Streamline : Failed to set Reflex option");
#endif
}

// ----------------------------------------------------------------------------------------------------
bool SLWrapper::SL_DLSS_Evaluate() // plan Section 4 (corrected) + Addendum D call site
{
#if HAS_STREAMLINE
    if (!m_bDlssInit)
        return false;

    sl::Constants consts{};
    consts.mvecScale.x = -1.0f;
    consts.mvecScale.y = -1.0f;
    consts.cameraNear  = Device.ViewportNear;
    consts.cameraFar   = g_pGamePersistent->pEnvironment->CurrentEnv->far_plane; // verify field on activation

    // BINARY-EXACT (SL_DLSS_Evaluate dump): raw Halton, +x / -y, no extra scaling. The binary runs the
    // same gamedata shaders (ssfx_taa_jitter applies the binder constant directly) and works with these
    // values -- ground truth beats derivation.
    const u32 j = Device.dwFrame % 72;
    consts.jitterOffset.x =  Device.HaltonJittering[j].x;
    consts.jitterOffset.y = -Device.HaltonJittering[j].y;

    consts.cameraFOV         = Device.fFOV * (3.14159265f / 180.0f);          // CORRECTED: no * 0.5
    consts.cameraAspectRatio = (float)Device.Real_Width / (float)Device.Real_Height;
    consts.minRelativeLinearDepthObjectSeparation = 40.0f;                    // CORRECTED: was 10.0
    consts.motionVectorsInvalidValue              = -FLT_MAX;                 // CORRECTED: sign
    consts.depthInverted        = sl::Boolean::eFalse;
    consts.cameraMotionIncluded = sl::Boolean::eTrue;                        // CORRECTED: 0x100 write

    slSetConstants(consts, *SL_Frame(), g_sl_viewport);

    // Input = rt_Generic_0 (post-lut, pre-combine_2 -- the binary's Addendum D placement, also closer to
    // the NVIDIA guide's "before post-processing"). Output = rt_sceneAA at Target_*. Depth = rt_tempzb
    // (Target_*-sized) read via the Real extent.
    sl::Resource colorIn { sl::ResourceType::eTex2d, RImplementation.Target->rt_Generic_0->pSurface };
    sl::Resource colorOut{ sl::ResourceType::eTex2d, RImplementation.Target->rt_sceneAA->pSurface };
    sl::Resource depth   { sl::ResourceType::eTex2d, RImplementation.Target->rt_tempzb->pSurface };
    sl::Resource mvec    { sl::ResourceType::eTex2d, RImplementation.Target->rt_ssfx_motion_vectors->pSurface };

    sl::Extent extRender{ 0, 0, Device.Real_Width,   Device.Real_Height };
    sl::Extent extTarget{ 0, 0, Device.Target_Width, Device.Target_Height };

    sl::ResourceTag inputs[4] = {
        sl::ResourceTag{ &colorIn,  sl::kBufferTypeScalingInputColor,  sl::ResourceLifecycle::eValidUntilPresent, &extRender },
        sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &extTarget },
        sl::ResourceTag{ &depth,    sl::kBufferTypeDepth,              sl::ResourceLifecycle::eValidUntilPresent, &extRender },
        sl::ResourceTag{ &mvec,     sl::kBufferTypeMotionVectors,      sl::ResourceLifecycle::eValidUntilPresent, &extRender },
    };
    slSetTagForFrame(*SL_Frame(), g_sl_viewport, inputs, 4, HW.pContext);

    const sl::BaseStructure* evalInputs[1] = { &g_sl_viewport };
    sl::Result r = slEvaluateFeature(sl::kFeatureDLSS, *SL_Frame(), evalInputs, 1, HW.pContext);
    if (r != sl::Result::eOk)
    {
        // Log once -- this runs every frame; per-frame Msg() tanks the framerate via log I/O.
        static sl::Result last_logged = sl::Result::eOk;
        if (r != last_logged)
        {
            Msg("! NV Streamline : Failed the DLSS evaluation [ %s ]", sl::getResultAsStr(r));
            last_logged = r;
        }
        return false;
    }
    return true;
#else
    return false;
#endif
}

// ----------------------------------------------------------------------------------------------------
void SLWrapper::SL_Shutdown() // plan Section 5d
{
#if HAS_STREAMLINE
    if (!m_SLInit)
        return;
    if (slShutdown() != sl::Result::eOk)
        Msg("! NV Streamline : Failed to shutdown");
    else
        Msg("- NV Streamline : Successful shutdown");
    m_SLInit = false;
#endif
}

// ----------------------------------------------------------------------------------------------------
void SLWrapper::SL_NewFrameToken()
{
#if HAS_STREAMLINE
    slGetNewFrameToken(reinterpret_cast<sl::FrameToken*&>(Device.PCL_currentFrame), nullptr);
#endif
}

void SLWrapper::SL_PCLMarker(int marker)
{
#if HAS_STREAMLINE
    if (Device.slDoPCL && Device.PCL_currentFrame)
        slPCLSetMarker((sl::PCLMarker)marker, *SL_Frame());
#endif
}

void SLWrapper::SL_ReflexSleep()
{
#if HAS_STREAMLINE
    if (m_bReflexInit && Device.PCL_currentFrame)
        slReflexSleep(*SL_Frame());
#endif
}
