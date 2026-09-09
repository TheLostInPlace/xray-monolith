#include "stdafx.h"
#pragma hdrstop

#include "blender_hdr10_ui_composite.h"

CBlender_hdr10_ui_composite::CBlender_hdr10_ui_composite()	{ description.CLS = 0; }
CBlender_hdr10_ui_composite::~CBlender_hdr10_ui_composite()	{	}

void CBlender_hdr10_ui_composite::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "hdr10_ui_composite", FALSE, FALSE, FALSE);

	C.r_dx10Texture("s_hdr10_present", r4_RT_HDR10_present);
	C.r_dx10Texture("s_hdr10_ui", r4_RT_HDR10_ui);
	C.r_dx10Sampler("smp_rtlinear");
	C.r_End();
}
