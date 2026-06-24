// shadow_ext_model.vs : shadow-map caster VS for external (GLTF/GLB) models. Identical to the stock
// shadow_direct_model.vs (transform position to the light's clip space + pass depth) EXCEPT it adds a
// NORMAL-OFFSET depth bias: the vertex is pushed slightly along its normal before projection.
//
// Why: the stock model shadow uses default cull + the engine's depth bias, which is tuned for typical
// (busy) OGF geometry. Our imported assets are often either very smooth+large (self-shadow ACNE) or very
// thin (close dynamic lights miss/blacken them). A small normal offset fixes BOTH without back-face
// casting (which broke thin geometry). This is OUR shadow VS only -- stock OGF keeps shadow_direct_model,
// so nothing stock changes. Supports SKIN_NONE (external statics) AND SKIN_0..4 (skinned externals --
// the bias is applied to the DEFORMED vertex/normal, so skinned shadows track the animated pose). The
// engine picks the variant from SetSkinningMode, exactly like the lit pass.
//
// Tune EXT_SHADOW_NORMAL_OFFSET below: too small -> acne/self-shadow returns; too large -> the shadow
// detaches from the caster ("peter-panning") / light leaks at contact.

#include "common.h"
#include "skin.h"

#define EXT_SHADOW_NORMAL_OFFSET 0.035h   // model-space units (assets auto-scale to ~2u)

v2p_shadow_direct _main( v_model I )
{
	v2p_shadow_direct O;

	// normal-offset bias: nudge the caster along its surface normal (toward the light on lit faces) so
	// the lit surface sits in front of its own shadow depth.
	float3 Pb   = I.P.xyz + normalize( (float3)I.N ) * EXT_SHADOW_NORMAL_OFFSET;
	O.hpos      = mul( m_WVP, float4( Pb, 1.0h ) );
#ifndef USE_HWSMAP
	O.depth     = O.hpos.z;
#endif
	return O;
}

#ifdef SKIN_NONE
v2p_shadow_direct main( v_model v ) { return _main(v); }
#endif

// Skinned variants: skinning_N (skin.h) deforms the vertex to v_model, then _main biases + projects it.
#ifdef SKIN_0
v2p_shadow_direct main( v_model_skinned_0 v ) { return _main(skinning_0(v)); }
#endif
#ifdef SKIN_1
v2p_shadow_direct main( v_model_skinned_1 v ) { return _main(skinning_1(v)); }
#endif
#ifdef SKIN_2
v2p_shadow_direct main( v_model_skinned_2 v ) { return _main(skinning_2(v)); }
#endif
#ifdef SKIN_3
v2p_shadow_direct main( v_model_skinned_3 v ) { return _main(skinning_3(v)); }
#endif
#ifdef SKIN_4
v2p_shadow_direct main( v_model_skinned_4 v ) { return _main(skinning_4(v)); }
#endif

FXVS;
