#include "stdafx.h"

// nonzero while the hdr ui layer is the bound target
u32 g_hdr10_ui_layer_live = 0;

static u32 g_hdr10_ui_proof = 0;

static void PushFSQ(FVF::TL* pv, float w, float h)
{
    u32   C   = color_rgba(0, 0, 0, 255);
    float d_Z = EPS_S;
    float d_W = 1.0f;

    pv->set(0, h, d_Z, d_W, C, 0.0f, 1.0f); pv++;
    pv->set(0, 0, d_Z, d_W, C, 0.0f, 0.0f); pv++;
    pv->set(w, h, d_Z, d_W, C, 1.0f, 1.0f); pv++;
    pv->set(w, 0, d_Z, d_W, C, 1.0f, 0.0f); pv++;
}

ID3DRenderTargetView* CRenderTarget::hdr10_ui_rt()
{
    if (g_hdr10_ui_layer_live) return rt_HDR10_ui->pRT;
    return HW.pBaseRT;
}

void CRenderTarget::phase_hdr10_ui_begin()
{
    if (!RImplementation.o.dx11_hdr10) return;
    if (!rt_HDR10_ui || !rt_HDR10_ui->valid() || !rt_HDR10_present || !rt_HDR10_present->valid()) return;
    if (g_hdr10_ui_layer_live)
    {
        // the caller rebound the back buffer so re-assert the layer target
        u_setrt(rt_HDR10_ui, NULL, NULL, HW.pBaseZB);
        return;
    }

    FLOAT ClearRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    HW.pContext->ClearRenderTargetView(rt_HDR10_ui->pRT, ClearRGBA);
    u_setrt(rt_HDR10_ui, NULL, NULL, HW.pBaseZB);

    g_hdr10_ui_layer_live = 1;

    // the ui nits lane changes with the stage so drop the cached table
    RCache.set_Constants((R_constant_table*)0);
}

void CRenderTarget::phase_hdr10_ui_rebind()
{
    if (!g_hdr10_ui_layer_live) return;

    // a third party pass may have moved the output merger under the cached pRT so force the bind
    RCache.set_RT(NULL);
    u_setrt(rt_HDR10_ui, NULL, NULL, HW.pBaseZB);
}

// the reshade call site lives in xrGame so it reaches the rebind through the render interface
void CRender::hdr10_ui_rebind()
{
    if (!o.dx11_hdr10) return;
    if (Target) Target->phase_hdr10_ui_rebind();
}

void CRenderTarget::phase_hdr10_ui_composite()
{
    if (!g_hdr10_ui_layer_live) return;

    g_hdr10_ui_layer_live = 0;
    RCache.set_Constants((R_constant_table*)0);

    float orig_w = float(Device.dwWidth);
    float orig_h = float(Device.dwHeight);

    // the swapchain buffer carries no shader input usage so the world half arrives by copy
    ID3DResource* back = NULL;
    HW.pBaseRT->GetResource(&back);
    if (back)
    {
        HW.pContext->CopyResource(rt_HDR10_present->pSurface, back);
        _RELEASE(back);
    }

    RCache.set_Z(FALSE);

    u32 Offset = 0;
    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    PushFSQ(pv, orig_w, orig_h);
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // bind the back buffer before the element so the layer can be sampled
    u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, NULL, NULL, HW.pBaseZB);
    RCache.set_Element(s_hdr10_ui_composite->E[0]);
    RCache.set_Geometry(g_combine);
    set_viewport_size(HW.pContext, orig_w, orig_h);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    RCache.set_Z(TRUE);

    if (0 == g_hdr10_ui_proof++)
        Msg("* [HDR10-UI] composite live, layer %dx%d F16, saturation knob deleted", Device.dwWidth, Device.dwHeight);
}
