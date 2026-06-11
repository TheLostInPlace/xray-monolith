#include "stdafx.h"

void CRenderTarget::phase_gasmask_drops()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
#if defined(USE_DX10) || defined(USE_DX11)	
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
#else
	p0.set(0.5f / w, 0.5f / h);
	p1.set((w + 0.5f) / w, (h + 0.5f) / h);
#endif
	
	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX10) || defined(USE_DX11)
#if RENDER == R_R4
	// SSS UPDATE 24 -- binary (0x800 path): render into rt_sceneFinal, then refresh rt_sceneAA from it.
	ref_rt& dest_rt = rt_sceneFinal ? rt_sceneFinal : (RImplementation.o.dx10_msaa ? rt_Generic : rt_Color);
#else
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
#endif
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif		

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_gasmask_drops->E[0]);
	
	//Set parameters
	RCache.set_c("drops_control", ps_r2_drops_control.x, ps_r2_drops_control.y, ps_r2_drops_control.z, 0);
	RCache.set_c("mask_control", ps_r2_mask_control.x, ps_r2_mask_control.y, ps_r2_mask_control.z, ps_r2_mask_control.w);	

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	
#if defined(USE_DX10) || defined(USE_DX11)
#if RENDER == R_R4
	// Binary: CopyResource(rt_sceneAA <- rt_sceneFinal) on the 0x800 path, generic0 <- dest otherwise.
	if (rt_sceneFinal && rt_sceneAA && dest_rt._get() == rt_sceneFinal._get())
		HW.pContext->CopyResource(rt_sceneAA->pTexture->surface_get(), rt_sceneFinal->pTexture->surface_get());
	else
		HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#else
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
#endif
};
