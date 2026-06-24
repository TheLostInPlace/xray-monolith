// deffer_model_bump_vc.vs : vertex shader for external (GLTF/GLB) NORMAL-MAPPED models.
//
// Self-contained: it does NOT depend on the un-shipped stock deffer_model_bump.vs. It mirrors
// deffer_model_flat_vc.vs (eye-space pos + hemi-cube ambient, COLOR_0 passthrough) and additionally
// forwards the eye-space tangent/binormal/normal (Te/Be/Ne) so the external bump PS can build the TBN
// itself, plus both UV sets (UV0 + glTF TEXCOORD_1) for per-map texCoord routing. SKIN_NONE only
// (external statics never skin -- the skinned path keeps the stock model VS + deffer_base_ext_flat).

#include "common.h"

struct v_model_bx
{
	float4 P     : POSITION;
	float3 N     : NORMAL;
	float3 T     : TANGENT;
	float3 B     : BINORMAL;
	float2 tc    : TEXCOORD0;
	float2 tc1   : TEXCOORD1;     // glTF TEXCOORD_1 (2nd UV set)
	float4 color : COLOR0;        // glTF COLOR_0
};

struct v2p_bump_ext
{
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float4 tcdh     : TEXCOORD0;     // tc, w = sun-occlusion
#else
	float2 tcdh     : TEXCOORD0;
#endif
	float4 position : TEXCOORD1;     // eye-space pos + hemi
	float3 Te       : TEXCOORD2;     // eye-space tangent
	float3 Be       : TEXCOORD3;     // eye-space binormal
	float3 Ne       : TEXCOORD4;     // eye-space normal
	float4 vcolor   : TEXCOORD5;     // vertex colour
	float2 tc1      : TEXCOORD6;     // glTF 2nd UV set (raw)
	float4 hpos     : SV_Position;
};

#ifdef SKIN_NONE
v2p_bump_ext main( v_model_bx I )
{
	v2p_bump_ext O;

	float3 Pe = mul( m_WV, I.P );
	O.hpos    = mul( m_WVP, I.P );

	// eye-space TBN (the PS builds the tangent->eye basis from these). Normalized in the PS.
	O.Te = mul( (float3x3)m_WV, (float3)I.T );
	O.Be = mul( (float3x3)m_WV, (float3)I.B );
	O.Ne = mul( (float3x3)m_WV, (float3)I.N );

	O.tcdh = float4( I.tc.xyyy );
	O.tc1  = I.tc1;

	// hemi-cube ambient (identical to deffer_model_flat_vc.vs)
	float3 Nw       = mul( (float3x3)m_W, (float3)I.N );
	float3 hc_pos   = (float3)hemi_cube_pos_faces;
	float3 hc_neg   = (float3)hemi_cube_neg_faces;
	float3 hc_mixed = (Nw < 0) ? hc_neg : hc_pos;
	float  hemi_val = saturate( dot( hc_mixed, abs(Nw) ) );

	O.position = float4( Pe, hemi_val );

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	O.tcdh.w = L_material.y;
#endif

	O.vcolor = I.color;
	return O;
}
#endif

FXVS;
