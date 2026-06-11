#include "stdafx.h"

void CRenderTarget::phase_nightvision()
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
#if RENDER == R_R4
	// SSS UPDATE 24 -- binary (0x800 path): render into rt_sceneFinal (the blender reads scene_aa),
	// then refresh scene_aa from it so the rest of the post chain sees the NVG image.
	ref_rt& dest_rt = rt_sceneFinal ? rt_sceneFinal : (RImplementation.o.dx10_msaa ? rt_Generic : rt_Color);
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#elif defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
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
	RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

#if RENDER == R_R4
	// Binary: CopyResource(rt_sceneAA <- rt_sceneFinal) on the 0x800 path, generic0 <- dest otherwise.
	if (rt_sceneFinal && rt_sceneAA && dest_rt._get() == rt_sceneFinal._get())
		HW.pContext->CopyResource(rt_sceneAA->pTexture->surface_get(), rt_sceneFinal->pTexture->surface_get());
	else
		HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#elif defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
};


//crookr
void CRenderTarget::phase_fakescope()
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
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);

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
	RCache.set_Element(s_fakescope->E[ps_r2_nightvision]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#else
	//Main pass (we avoid write-read from the same buffer)
	u_setrt(rt_Generic_PingPong, nullptr, nullptr, nullptr);

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
	RCache.set_Element(s_fakescope->E[0]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//Draw to rt_Generic_0
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_fakescope->E[1]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
#endif
};

//--DSR-- HeatVision_start
void CRenderTarget::phase_heatvision()
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
#if RENDER == R_R4
	// SSS UPDATE 24 -- binary (0x800 path): render into rt_sceneFinal, refresh rt_sceneAA after.
	// NOTE: the binary heatvision blender still samples generic0 (verified -- no scene_aa switch).
	ref_rt& dest_rt = rt_sceneFinal ? rt_sceneFinal : (RImplementation.o.dx10_msaa ? rt_Generic : rt_Color);
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#elif defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
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
	RCache.set_Element(s_heatvision->E[ps_r2_heatvision]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

#if RENDER == R_R4
	if (rt_sceneFinal && rt_sceneAA && dest_rt._get() == rt_sceneFinal._get())
		HW.pContext->CopyResource(rt_sceneAA->pTexture->surface_get(), rt_sceneFinal->pTexture->surface_get());
	else
		HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#elif defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
};

#if RENDER == R_R4
// SSS UPDATE 24 -- new in the binary: temporal heat-buffer build, called from phase_combine right
// after phase_lut every frame. Frame-limited by heat_vision_hertz (0 = every frame): draws
// s_heatvision->E[2] ("heatvision_upscale") into rt_ssfx_temp2, copies it to rt_Heat, then
// rt_Heat -> rt_Heat_prev. On limited frames it copies rt_Heat_prev -> rt_Heat instead.
extern int heat_vision_hertz;
void CRenderTarget::phase_heatvision_build()
{
	static u32 HVFrameLimit = 0;
	if (Device.dwTimeGlobal > HVFrameLimit)
	{
		HVFrameLimit = Device.dwTimeGlobal + (heat_vision_hertz > 0 ? 1000 / heat_vision_hertz : 0);

		u32 Offset = 0;
		u32 C = color_rgba(0, 0, 0, 255);
		float d_Z = EPS_S, d_W = 1.0f;

		u_setrt(rt_ssfx_temp2, nullptr, nullptr, nullptr);
		float w = float(dwWidth);
		float h = float(dwHeight);

		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, float(h), d_Z, d_W, C, 0.0f, 1.0f); pv++;
		pv->set(0, 0, d_Z, d_W, C, 0.0f, 0.0f); pv++;
		pv->set(float(w), float(h), d_Z, d_W, C, 1.0f, 1.0f); pv++;
		pv->set(float(w), 0, d_Z, d_W, C, 1.0f, 0.0f); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		RCache.set_Element(s_heatvision->E[2]);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

		HW.pContext->CopyResource(rt_Heat->pTexture->surface_get(), rt_ssfx_temp2->pTexture->surface_get());
		HW.pContext->CopyResource(rt_Heat_prev->pTexture->surface_get(), rt_Heat->pTexture->surface_get());
	}
	else
	{
		HW.pContext->CopyResource(rt_Heat->pTexture->surface_get(), rt_Heat_prev->pTexture->surface_get());
	}
};
#endif
//--DSR-- HeatVision_start

#if defined(USE_DX11)	//  Redotix99: for 3D Shader Based Scopes 		(sorry for using the nightvision phase file)
void CRenderTarget::phase_3DSSReticle()
{
#if RENDER == R_R4
	// SSS UPDATE 24 -- binary: s_position feed (generic2 <- position), then scene_final <- scene_aa
	// (the reticle shader samples s_prev_frame = "$user$scene_final" for the behind-glass image),
	// and the reticle geometry is drawn into rt_sceneAA alone with NO depth buffer bound.
	HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());

	HW.pContext->CopyResource(rt_sceneFinal->pTexture->surface_get(), rt_sceneAA->pTexture->surface_get());

	u_setrt(rt_sceneAA, 0, 0, 0);
#else
	HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());

	HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());

	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
#endif

	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	RImplementation.render_Reticle();
};
#endif
