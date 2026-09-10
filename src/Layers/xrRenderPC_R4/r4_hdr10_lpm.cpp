#include "stdafx.h"

#include <stdint.h>
#include <math.h>

#define A_CPU 1
#include "../../3rd party/FidelityFX-LPM/ffx_a.h"

// LpmSetup writes the control block through this, one uint4 per index
static AU1 g_lpm_ctl[24 * 4];
static void LpmSetupOut(AU1 i, inAU4 v)
{
	g_lpm_ctl[i * 4 + 0] = v[0];
	g_lpm_ctl[i * 4 + 1] = v[1];
	g_lpm_ctl[i * 4 + 2] = v[2];
	g_lpm_ctl[i * 4 + 3] = v[3];
}

#include "../../3rd party/FidelityFX-LPM/ffx_lpm.h"

// the setup inputs the last control block was built from
struct lpm_setup_inputs
{
	float hdr_max;
	float exposure;
	float hdr10_s;
	float contrast;
	float shoulder_contrast;
	float saturation;
	float crosstalk_r;
	float crosstalk_g;
	float crosstalk_b;
};

static lpm_setup_inputs g_lpm_last = {};
static bool g_lpm_valid = false;

static lpm_setup_inputs hdr10_lpm_inputs()
{
	lpm_setup_inputs in;
	in.hdr_max = hdr10_headroom();
	// the mapper anchors its own 0.18 input on mid level so the exposure cvar stays a plain grading gain
	in.exposure = ALog2F1(in.hdr_max);
	in.hdr10_s = LpmHdr10RawScalar(ps_r4_hdr10_whitepoint_nits);
	in.contrast = ps_r4_hdr10_lpm_contrast;
	in.shoulder_contrast = ps_r4_hdr10_lpm_shoulder_contrast;
	in.saturation = ps_r4_hdr10_lpm_saturation;
	in.crosstalk_r = ps_r4_hdr10_lpm_crosstalk_r;
	in.crosstalk_g = ps_r4_hdr10_lpm_crosstalk_g;
	in.crosstalk_b = ps_r4_hdr10_lpm_crosstalk_b;

	// lpm wants one crosstalk channel at 1.0 and the others above zero
	const float crosstalk_peak = _max(in.crosstalk_r, _max(in.crosstalk_g, in.crosstalk_b));
	in.crosstalk_r /= crosstalk_peak;
	in.crosstalk_g /= crosstalk_peak;
	in.crosstalk_b /= crosstalk_peak;

	return in;
}

static bool lpm_inputs_equal(const lpm_setup_inputs& a, const lpm_setup_inputs& b)
{
	return 0 == memcmp(&a, &b, sizeof(lpm_setup_inputs));
}

// rebuilds the control block when a cvar moved and hands back the 24 uint4 values
const u32* hdr10_lpm_control()
{
	const lpm_setup_inputs in = hdr10_lpm_inputs();

	if (!g_lpm_valid || !lpm_inputs_equal(in, g_lpm_last))
	{
		AF1 hdr10S = in.hdr10_s;
		varAF3(saturation) = initAF3(in.saturation, in.saturation, in.saturation);
		varAF3(crosstalk) = initAF3(in.crosstalk_r, in.crosstalk_g, in.crosstalk_b);

		// soft gamut is off in the hdr10 prefab so the gap argument is unused
		LpmSetup(
			A_TRUE, LPM_CONFIG_HDR10RAW_709, LPM_COLORS_HDR10RAW_709,
			0.0f,
			in.hdr_max,
			in.exposure,
			in.contrast,
			in.shoulder_contrast,
			saturation, crosstalk);

		static bool reported = false;
		if (!reported)
		{
			reported = true;
			Msg("* HDR10 lpm setup: hdrMax %.3f exposure %.3f hdr10S %.5f contrast %.3f shoulder %.3f saturation %.3f crosstalk %.3f %.3f %.3f",
				in.hdr_max, in.exposure, in.hdr10_s, in.contrast, in.shoulder_contrast, in.saturation,
				in.crosstalk_r, in.crosstalk_g, in.crosstalk_b);
		}

		g_lpm_last = in;
		g_lpm_valid = true;
	}

	return (const u32*)g_lpm_ctl;
}
