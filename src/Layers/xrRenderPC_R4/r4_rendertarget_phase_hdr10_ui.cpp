#include "stdafx.h"
#include "../xrRenderDX10/StateManager/dx10StateCache.h"

// nonzero while the hdr ui layer is the bound target
u32 g_hdr10_ui_layer_live = 0;

static u32 g_hdr10_ui_proof = 0;

// builds the layer variant of a blend state once, the cache clears when the device changes
static ID3DBlendState* hdr10_ui_layer_variant(ID3DBlendState* base)
{
	D3D_BLEND_DESC desc;
	dx10StateUtils::ResetDescription(desc);
	D3D_BLEND_DESC src;
	dx10StateUtils::ResetDescription(src);
	base->GetDesc(&src);

	// copy field by field so the hash never sees stale padding
	desc.AlphaToCoverageEnable = src.AlphaToCoverageEnable;
	desc.IndependentBlendEnable = src.IndependentBlendEnable;
	for (u32 i = 0; i < 8; ++i)
	{
		desc.RenderTarget[i].BlendEnable = src.RenderTarget[i].BlendEnable;
		desc.RenderTarget[i].SrcBlend = src.RenderTarget[i].SrcBlend;
		desc.RenderTarget[i].DestBlend = src.RenderTarget[i].DestBlend;
		desc.RenderTarget[i].BlendOp = src.RenderTarget[i].BlendOp;
		desc.RenderTarget[i].SrcBlendAlpha = src.RenderTarget[i].SrcBlendAlpha;
		desc.RenderTarget[i].DestBlendAlpha = src.RenderTarget[i].DestBlendAlpha;
		desc.RenderTarget[i].BlendOpAlpha = src.RenderTarget[i].BlendOpAlpha;
		desc.RenderTarget[i].RenderTargetWriteMask = src.RenderTarget[i].RenderTargetWriteMask;
	}
	D3D11_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];

	const u8 rgb = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
	if (0 == (rt.RenderTargetWriteMask & rgb)) return base;

	const bool over = rt.BlendEnable && D3D_BLEND_SRC_ALPHA == rt.SrcBlend && D3D_BLEND_INV_SRC_ALPHA == rt.DestBlend;
	const bool opaque = !rt.BlendEnable;
	if (!over && !opaque) return base;

	const bool coverage = (rt.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALPHA)
		&& D3D_BLEND_ONE == rt.SrcBlendAlpha && D3D_BLEND_INV_SRC_ALPHA == rt.DestBlendAlpha;
	if (over && coverage) return base;

	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D_BLEND_ONE;
	rt.DestBlendAlpha = D3D_BLEND_INV_SRC_ALPHA;
	rt.BlendOpAlpha = D3D_BLEND_OP_ADD;
	rt.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
	return BSManager.GetState(desc);
}

// every pass drawn into the layer blends coverage into alpha so the composite can cover the world
ID3DBlendState* hdr10_ui_layer_blend(ID3DBlendState* base)
{
	if (!g_hdr10_ui_layer_live || !base) return base;

	static xr_map<ID3DBlendState*, ID3DBlendState*> variants;
	static ID3DDevice* variants_device = NULL;
	if (variants_device != HW.pDevice)
	{
		variants.clear();
		variants_device = HW.pDevice;
	}

	xr_map<ID3DBlendState*, ID3DBlendState*>::iterator it = variants.find(base);
	if (it != variants.end()) return it->second;

	ID3DBlendState* variant = hdr10_ui_layer_variant(base);
	variants.insert(std::make_pair(base, variant));
	return variant;
}

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
