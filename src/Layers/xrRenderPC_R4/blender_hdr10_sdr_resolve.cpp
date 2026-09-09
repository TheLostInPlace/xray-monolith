#include "stdafx.h"
#pragma hdrstop

#include "blender_hdr10_sdr_resolve.h"

CBlender_hdr10_sdr_resolve::CBlender_hdr10_sdr_resolve()	{ description.CLS = 0; }
CBlender_hdr10_sdr_resolve::~CBlender_hdr10_sdr_resolve()	{	}

void CBlender_hdr10_sdr_resolve::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "hdr10_sdr_resolve", FALSE, FALSE, FALSE);
	C.r_dx10Texture("s_hdr10_game", r2_RT_albedo);
	C.r_dx10Sampler("smp_rtlinear");
	C.r_End();
}
