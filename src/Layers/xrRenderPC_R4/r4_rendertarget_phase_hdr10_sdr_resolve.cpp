#include "stdafx.h"

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

// resolves the pre encode frame into an 8 bit sRGB target for capture
void CRenderTarget::phase_hdr10_sdr_resolve()
{
    if (!RImplementation.o.dx11_hdr10)
        return;

    RCache.set_Z(FALSE);

    float w = float(Device.dwWidth);
    float h = float(Device.dwHeight);

    u32 Offset = 0;

    FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
    PushFSQ(pv, w, h);
    RCache.Vertex.Unlock(4, g_combine->vb_stride);

    // bind the target before the source so rt_Color is never SRV and RTV at once
    set_viewport_size(HW.pContext, w, h);
    u_setrt(rt_HDR10_SDR, NULL, NULL, NULL);

    RCache.set_Element(s_hdr10_sdr_resolve->E[0]);
    RCache.set_Geometry(g_combine);

    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, NULL, NULL, HW.pBaseZB);
    RCache.set_Z(TRUE);
}
