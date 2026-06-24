// deffer_model_flat_vc.vs : vertex shader for external (GLTF/GLB) models that carry per-vertex colours
// (glTF COLOR_0). It mirrors the stock deffer_model_flat.vs exactly (eye-space pos/normal + hemi-cube
// ambient), but reads COLOR0 from our extended external vertex and passes it through to the PS, which
// uses it as the albedo (glTF vertex-colour models like BoxVertexColors have no base texture / UVs).
// SKIN_NONE only (external statics never skin).

#include "common.h"

struct v_model_vc
{
	float4 P     : POSITION;
	float3 N     : NORMAL;
	float3 T     : TANGENT;
	float3 B     : BINORMAL;
	float2 tc    : TEXCOORD0;
	float2 tc1   : TEXCOORD1;     // glTF TEXCOORD_1 (2nd UV set)
	float4 color : COLOR0;       // glTF COLOR_0 (our external vertex's extra element)
};

struct v2p_flat_vc
{
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float4 tcdh     : TEXCOORD0;     // tc, w = sun-occlusion
#else
	float2 tcdh     : TEXCOORD0;
#endif
	float4 position : TEXCOORD1;     // eye-space pos + hemi
	float3 N        : TEXCOORD2;     // eye-space normal
	float2 tc1      : TEXCOORD3;     // glTF 2nd UV set (raw)
	float4 vcolor   : TEXCOORD5;     // vertex colour
	float4 hpos     : SV_Position;
};

#ifdef SKIN_NONE
v2p_flat_vc main( v_model_vc I )
{
	v2p_flat_vc O;

	float3 Pe = mul( m_WV, I.P );
	O.hpos    = mul( m_WVP, I.P );
	O.N       = mul( (float3x3)m_WV, (float3)I.N );

	O.tcdh    = float4( I.tc.xyyy );
	O.tc1     = I.tc1;

	// hemi-cube ambient (identical to deffer_model_flat.vs)
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
